/*!
 * res_redis -- An open source telephony toolkit.
 *
 * Copyright (C) 2015, Diederik de Groot
 *
 * Diederik de Groot <ddegroot@users.sf.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*!
 * \file
 * \author Diederik de Groot <ddegroot@users.sf.net>
 *
 * This module is based on the res_corosync module.
 */

/*** MODULEINFO
	<depend>hiredis</depend>
	<support_level>extended</support_level>
 ***/


#include "config.h"
#include <asterisk.h>

#define AST_MODULE "res_redis"

ASTERISK_FILE_VERSION(__FILE__, "$Revision: 419592 $")
#include <hiredis/hiredis.h>
#include <hiredis/async.h>
#include <hiredis/adapters/libevent.h>

#include <asterisk/module.h>
#include <asterisk/logger.h>
#include <asterisk/config.h>
#include <asterisk/event.h>
#include <asterisk/cli.h>
#include <asterisk/netsock2.h>
#include <asterisk/devicestate.h>
#ifdef HAVE_PBX_STASIS_H
#include <asterisk/stasis.h>
#endif

#define AST_LOG_NOTICE_DEBUG(...) {ast_log(LOG_NOTICE, __VA_ARGS__);ast_debug(1, __VA_ARGS__);}

#include "../include/pbx_event_message_serializer.h"
#include "../include/shared.h"

/* globals */
AST_RWLOCK_DEFINE_STATIC(event_types_lock);
AST_MUTEX_DEFINE_STATIC(redis_lock);

#define MAX_EVENT_LENGTH 1024
pthread_t dispatch_thread_id = AST_PTHREADT_NULL;
struct event_base *eventbase = NULL;
unsigned int stoprunning = 0;
static redisAsyncContext *redisSubConn = NULL;
static redisAsyncContext *redisPubConn = NULL;
char default_servers[] = "127.0.0.1:6379";
char *servers = NULL;
char *curserver = NULL;
static char default_eid_str[32];

/* predeclarations */
#ifdef HAVE_PBX_STASIS_H
static void ast_event_cb(void *userdata, struct stasis_subscription *sub, struct stasis_message *smsg);
#else
static void ast_event_cb(const struct ast_event *event, void *data);
#endif
static void redis_dump_ast_event_cache();
static void redis_subscription_cb(redisAsyncContext *c, void *r, void *privdata);

static struct loc_event_type {
	const char *name;
#ifdef HAVE_PBX_STASIS_H
	struct stasis_subscription *sub;
#else
	struct ast_event_sub *sub;
#endif	
	unsigned char publish;
	unsigned char subscribe;
	unsigned char publish_default;
	unsigned char subscribe_default;
	char *channelstr;
	char *prefix;
} event_types[] = {
	[AST_EVENT_MWI] = { .name = "mwi"},
	[AST_EVENT_DEVICE_STATE_CHANGE] = { .name = "device_state_change"},
	[AST_EVENT_DEVICE_STATE] = { .name = "device_state"},
	[AST_EVENT_PING] = { .name = "ping", .publish_default = 1, .subscribe_default = 1 },
};

enum {
	PUBLISH,
	SUBSCRIBE,
	PREFIX,
};

static int redis_connect_nextserver()
{
	static char *remaining;
	char delims[] = ",";
	char *server;
	char *host;
	char *portstr;
	int port = 6379;
	
	if (redisPubConn) {
		//redisAsyncDisconnect(redisPubConn);
		//redisAsyncFree(redisPubConn);
	}
	if (redisSubConn) {
		redisAsyncCommand(redisSubConn, NULL, NULL, "UNSUBSCRIBE");
		
		//redisAsyncDisconnect(redisSubConn);
		//redisAsyncFree(redisSubConn);
	}
	if (!remaining) {
		strcat(servers, delims);
		curserver = strtok_r(servers, delims, &remaining);
	} else {
		curserver = strtok_r(NULL, delims, &remaining);
	}	
	while (servers && curserver) {
		server = ast_trim_blanks(ast_strdupa(ast_skip_blanks(curserver)));
		ast_debug(1, "Connecting to curserver:'%s', server:'%s' / servers:'%s' / remaining:'%s'\n", curserver, server, servers, remaining);
		if (server[0] == '/') {
			AST_LOG_NOTICE_DEBUG("Use Socket: %s\n", server);
			redisPubConn = redisAsyncConnectUnix(server);
			redisSubConn = redisAsyncConnectUnix(server);
		} else {
			ast_sockaddr_split_hostport(server, &host, &portstr, 0);
			if (!ast_strlen_zero(portstr)) {
				port = atoi(portstr);
			}
			AST_LOG_NOTICE_DEBUG("Use Server: '%s', Port: '%d'\n", server, port);
			if (!ast_strlen_zero(server)) {
				redisPubConn = redisAsyncConnect(server, port);
				redisSubConn = redisAsyncConnect(server, port);
			}
		}
		if (redisPubConn == NULL || redisPubConn->err || redisSubConn == NULL || redisSubConn->err) {
			if (redisPubConn || redisSubConn) {
				ast_log(LOG_ERROR, "Connection error: %s / %s\n", redisPubConn->errstr, redisSubConn->errstr);
			} else {
				ast_log(LOG_ERROR, "Connection error: Can't allocated redis context\n");
			}
			if (servers) {
				curserver = strtok_r(NULL, delims, &remaining);
				continue;
			} else {
				return 0;
			}
		}
		AST_LOG_NOTICE_DEBUG("Async Connection Started %s\n", curserver);
		break;
	}
	redis_dump_ast_event_cache();
	return -1;
}

void redis_pong_cb(redisAsyncContext *c, void *r, void *privdata) {
	redisReply *reply = r;
	if (reply == NULL) {
		goto cleanup;
	}
	AST_LOG_NOTICE_DEBUG("Pong\n");
cleanup:
//	freeReplyObject(reply);
	AST_LOG_NOTICE_DEBUG("Return from CB\n");
}

void redis_meet_cb(redisAsyncContext *c, void *r, void *privdata) {
	redisReply *reply = r;
	if (reply == NULL) {
		goto cleanup;
	}
	AST_LOG_NOTICE_DEBUG("Meet\n");
cleanup:
//	freeReplyObject(reply);
	AST_LOG_NOTICE_DEBUG("Return from CB\n");
}

static void redis_subscription_cb(redisAsyncContext *c, void *r, void *privdata) 
{
	 exception_t res = GENERAL_EXCEPTION;
	log_verbose(2, "res_redis: Enter (%s)\n", __PRETTY_FUNCTION__);
#ifndef HAVE_PBX_STASIS_H
	enum ast_event_type event_type;
	redisReply *reply = r;
	if (reply == NULL) {
		goto cleanup;
	}
	if (reply->type == REDIS_REPLY_ARRAY) {
		if (!strcasecmp(reply->element[0]->str, "MESSAGE")) {
			struct loc_event_type *etype;
			if (!ast_strlen_zero(reply->element[1]->str)) {
				for (event_type = 0; event_type < ARRAY_LEN(event_types); event_type++) {
					ast_rwlock_rdlock(&event_types_lock);
					if (!event_types[event_type].channelstr) {
						ast_rwlock_unlock(&event_types_lock);
						continue;
					}
					if (!strcmp(event_types[event_type].channelstr, reply->element[1]->str)) {
						etype = &event_types[event_type];
						ast_rwlock_unlock(&event_types_lock);
						break;
					}
					ast_rwlock_unlock(&event_types_lock);
				}
			
				if (etype) {
					if (!ast_strlen_zero(reply->element[2]->str)) {
						ast_debug(1, "start decoding'\n");
					
						if (etype->publish) {
							if (!strcasecmp(reply->element[1]->str, etype->channelstr)) {
#ifdef HAVE_PBX_STASIS_H
									
#else
								struct ast_event *event = NULL;
								char *msg = ast_strdupa(reply->element[2]->str);
								unsigned int res = 0;
								
								if (strlen(reply->element[2]->str) < ast_event_minimum_length()) {
									ast_log(LOG_ERROR, "Ignoring event that's too small. %u < %u\n", (unsigned int) strlen(reply->element[2]->str), (unsigned int) ast_event_minimum_length());
									goto cleanup;
								}
								if ((res = json2message(&event, event_type, msg))) {
									if (res == EID_SELF) {
										// skip feeding back to self
										ast_debug(1, "Originated Here. skip'\n");
										goto cleanup;
									} else {
										/*
										// check decoding
										char eid_str1[32];
										const void *eid = ast_event_get_ie_raw(event, AST_EVENT_IE_EID);
										if (eid) {
											ast_eid_to_str(eid_str1, sizeof(eid_str1), (void *)eid);
										}
										ast_debug(1, "event: device: '%s'\n", ast_event_get_ie_str(event, AST_EVENT_IE_DEVICE));
										ast_debug(1, "event: state: '%u'\n", ast_event_get_ie_uint(event, AST_EVENT_IE_STATE));
										ast_debug(1, "event: device: '%u'\n", ast_event_get_ie_uint(event, AST_EVENT_IE_CACHABLE));
										ast_debug(1, "event: eid: '%s' / %p\n", eid_str1, eid);
										*/
										if (res == OK) {
#ifdef HAVE_PBX_STASIS_H
											//ast_publish_device_state();
#else
											ast_event_queue(event);
#endif
										}
										if (res == OK_CACHABLE) {
#ifdef HAVE_PBX_STASIS_H
											//ast_publish_device_state();
#else
											ast_event_queue_and_cache(event);
#endif
										}
										ast_debug(1, "ast_event sent'\n");
										res = NO_EXCEPTION;
									}
								} else {
									ast_log(LOG_ERROR, "error decoding %s'\n", msg);
								}
#endif
							} else {
								ast_debug(1, "has different channelstr '%s'\n", etype->channelstr);
							}
						} else {
							ast_debug(1, "event_type should not be published\n");
						}
					} else {
						ast_debug(1, "message content is zero\n");
					}
				} else {
					ast_log(LOG_ERROR, "event_type does not exist'\n");
				}
			}
		} else {
			int j;
			for (j = 0; j < reply->elements; j++) {
				ast_debug(1, "REDIS_SUBSCRIPTION_CB: [%u]: %s\n", j, reply->element[j]->str);
			}
		}		
	}
#endif
cleanup:
//	freeReplyObject(reply);
	AST_LOG_NOTICE_DEBUG("Return from CB\n");
	log_verbose(2, "ast_redis: Exit %s%s\n", res ? ", Exception Occured: " : "", res ? exception2str[res].str : "");
}

void redis_connect_cb(const redisAsyncContext *c, int status) {
	if (status != REDIS_OK) {
		printf("Error: %s\n", c->errstr);
		return;
	}
	AST_LOG_NOTICE_DEBUG("Connected\n");
}

void redis_disconnect_cb(const redisAsyncContext *c, int status) {
	if (status != REDIS_OK) {
		printf("Error: %s\n", c->errstr);
		return;
	}
	AST_LOG_NOTICE_DEBUG("Disconnected\n");
	ast_mutex_lock(&redis_lock);
	if (!stoprunning) {
		redis_connect_nextserver();
	}
	ast_mutex_unlock(&redis_lock);
}


static void redis_dump_ast_event_cache()
{
	if (dispatch_thread_id != AST_PTHREADT_NULL) {
		ast_debug(1, "Dumping Ast Event Cache to %s\n", curserver);
		unsigned int i = 0;
		// flush all changes
		for (i = 0; i < ARRAY_LEN(event_types); i++) {
			ast_rwlock_rdlock(&event_types_lock);
			if (!event_types[i].publish) {
				ast_rwlock_unlock(&event_types_lock);
				ast_debug(1, "%s skipping not published\n", event_types[i].name);
				continue;
			}
			ast_rwlock_unlock(&event_types_lock);

			ast_debug(1, "subscribe %s\n", event_types[i].name);
#ifdef HAVE_PBX_STASIS_H
			struct stasis_subscription *event_sub;
			event_sub = stasis_subscribe(ast_device_state_topic_all(), ast_event_cb, NULL);
			usleep(500);
			ast_debug(1, "Dumping Past %s Events\n", event_types[i].name);
			stasis_cache_dump(ast_device_state_cache(), NULL);
//			stasis_cache_dump_by_eid();
			stasis_unsubscribe(event_sub)
			//destroy ? 
#else
			struct ast_event_sub *event_sub;
			event_sub = ast_event_subscribe_new(i, ast_event_cb, NULL);
			ast_event_sub_append_ie_raw(event_sub, AST_EVENT_IE_EID, &ast_eid_default, sizeof(ast_eid_default));
			usleep(500);
			ast_debug(1, "Dumping Past %s Events\n", event_types[i].name);
			ast_event_dump_cache(event_sub);
			ast_event_sub_destroy(event_sub);
#endif
		}
		AST_LOG_NOTICE_DEBUG("Ast Event Cache Dumped to %s\n", curserver);
	}
}

#ifdef HAVE_PBX_STASIS_H
static void ast_event_cb(void *userdata, struct stasis_subscription *sub, struct stasis_message *smsg)
#else
static void ast_event_cb(const struct ast_event *event, void *data)
#endif
{
	ast_debug(1, "ast_event_cb\n");
#ifndef HAVE_PBX_STASIS_H
	const struct ast_eid *eid;
	char eid_str[32] = "";
	eid = ast_event_get_ie_raw(event, AST_EVENT_IE_EID);
	ast_eid_to_str(eid_str, sizeof(eid_str), (struct ast_eid *) eid);
	//AST_LOG_NOTICE_DEBUG("(ast_event_cb) Got event with EID: '%s' / '%s'\n", eid_str, default_eid_str);
	
	if (ast_event_get_type(event) == AST_EVENT_PING) {
		AST_LOG_NOTICE_DEBUG("(ast_event_cb) Got event PING from server with EID: '%s' (Add Handler)\n", eid_str);
		/*
		if (ast_event_get_type(event) == AST_EVENT_PING) {
			const struct ast_eid *eid;
			char buf[128] = "";

			eid = ast_event_get_ie_raw(event, AST_EVENT_IE_EID);
			ast_eid_to_str(buf, sizeof(buf), (struct ast_eid *) eid);
			ast_debug(1, "(ast_deliver_event) Got event PING from server with EID: '%s'\n", buf);

			ast_event_queue(event);
		*/
		redisAsyncCommand(redisPubConn, redis_meet_cb, NULL, "MEET");
		if (redisPubConn->err) {
			ast_log(LOG_ERROR, "redisAsyncCommand Send error: %s\n", redisPubConn->errstr);
		}
		redisAsyncCommand(redisPubConn, redis_pong_cb, (char*)eid_str, "PING");
		if (redisPubConn->err) {
			ast_log(LOG_ERROR, "redisAsyncCommand Send error: %s\n", redisPubConn->errstr);
		}
	}
	
	if (ast_eid_cmp(&ast_eid_default, eid)) {
		// If the event didn't originate from this server, don't send it back out.
		ast_debug(1, "(ast_event_cb) didn't originate on this server, don't send it back out, skipping: '%s')\n", eid_str);
		return;
	}
	AST_LOG_NOTICE_DEBUG("(ast_event_cb) Got event from EID: '%s'\n", eid_str);
#endif	
	
	// decode event2msg
	ast_debug(1, "(ast_event_cb) decode incoming message\n");
	struct loc_event_type *etype;
	char *msg = ast_alloca(MAX_EVENT_LENGTH + 1);
	if (!msg) {
		return /* MALLOC_ERROR */;
	}
	
	ast_rwlock_rdlock(&event_types_lock);
	etype = &event_types[ast_event_get_type(event)];
	ast_rwlock_unlock(&event_types_lock);
	
	if (etype) {
		if (etype->publish) {

#ifdef HAVE_PBX_STASIS_H
			const struct ast_eid *eid;
			char eid_str[32] = "";

			struct ast_json *
			if ((msg = stasis_message_to_json(smsg, NULL))) {
#else
			if (message2json(msg, MAX_EVENT_LENGTH, event)) {
#endif
				AST_LOG_NOTICE_DEBUG("sending 'PUBLISH %s \"%s\"'\n", etype->channelstr, msg);
				redisAsyncCommand(redisPubConn, NULL, NULL, "PUBLISH %s %b", etype->channelstr, msg, (size_t)strlen(msg));
				if (redisPubConn->err) {
					ast_log(LOG_ERROR, "redisAsyncCommand Send error: %s\n", redisPubConn->errstr);
				}
			} else {
				ast_log(LOG_ERROR, "error encoding %s'\n", msg);
			}
		} else {
			ast_debug(1, "event_type should not be published'\n");
		}
	} else {
		ast_log(LOG_ERROR, "event_type does not exist'\n");
	}
}

static int set_event(const char *event_type, int pubsub, char *str)
{
	unsigned int i;
	AST_LOG_NOTICE_DEBUG("set_event: %d, %s\n", pubsub, str);

	for (i = 0; i < ARRAY_LEN(event_types); i++) {
		if (!event_types[i].name || strcasecmp(event_type, event_types[i].name)) {
			continue;
		}
		switch (pubsub) {
			case PUBLISH:
				event_types[i].publish = 1;
				event_types[i].channelstr = str;
				break;
			case SUBSCRIBE:
				event_types[i].subscribe = 1;
				event_types[i].channelstr = str;
				break;
			case PREFIX:
				event_types[i].prefix = str;
				break;
		}

		break;
	}
	AST_LOG_NOTICE_DEBUG("set_event: returning: %d\n", (i == ARRAY_LEN(event_types)) ? -1 : 0);

	return (i == ARRAY_LEN(event_types)) ? -1 : 0;
}

static char *redis_show_members(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "res_redis show members";
		e->usage =
			"Usage: res_redis show members\n";
		return NULL;

	case CLI_GENERATE:
		return NULL;	/* no completion */
	}

	if (a->argc != e->args) {
		return CLI_SHOWUSAGE;
	}

	//if (!event) {
	//	return CLI_FAILURE;
	//}
	return CLI_SUCCESS;
}

static char *redis_show_config(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "res_redis config";
		e->usage = 
			"Usage: res_redis config\n";
		return NULL;

	case CLI_GENERATE:
		return NULL;	/* no completion */
	}

	if (a->argc != e->args) {
		return CLI_SHOWUSAGE;
	}

	//if (!event) {
	//	return CLI_FAILURE;
	//}
	return CLI_SUCCESS;
}

static char *redis_ping(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_event *event;

	switch (cmd) {
	case CLI_INIT:
		e->command = "res_redis ping/meet";
		e->usage =
			"Usage: res_redis ping/meet\n"
			"       Send a test ping to the cluster.\n"
			"A NOTICE will be in the log for every ping received\n"
			"on a server.\n  If you send a ping, you should see a NOTICE\n"
			"in the log for every server in the cluster.\n";
		return NULL;

	case CLI_GENERATE:
		return NULL;	/* no completion */
	}

	if (a->argc != e->args) {
		return CLI_SHOWUSAGE;
	}

	event = ast_event_new(AST_EVENT_PING, AST_EVENT_IE_END);

	if (!event) {
		return CLI_FAILURE;
	}

#ifdef HAVE_PBX_STASIS_H
	//ast_publish_device_state();
#else	
	ast_event_queue_and_cache(event);
#endif

	return CLI_SUCCESS;
}


static struct ast_cli_entry redis_cli[] = {
	AST_CLI_DEFINE(redis_show_config, "Show configuration"),
	AST_CLI_DEFINE(redis_show_members, "Show cluster members"),
	AST_CLI_DEFINE(redis_ping, "Send a test ping to the cluster"),
};

static int load_general_config(struct ast_config *cfg)
{
	struct ast_variable *v;
	int res = 0;

	ast_rwlock_wrlock(&event_types_lock);
	for (v = ast_variable_browse(cfg, "general"); v && !res; v = v->next) {
		if (!strcasecmp(v->name, "servers")) {
			if (servers) {
				ast_free(servers);
				servers = NULL;
			}
			servers = strdup(v->value);
			AST_LOG_NOTICE_DEBUG("Set Servers %s to '%s'\n", servers, v->value);
			
		} else if (!strcasecmp(v->name, "mwi_prefix")) {
			res = set_event("mwi", PREFIX, strdup(v->value)); 
		} else if (!strcasecmp(v->name, "publish_mwi_event")) {
			res = set_event("mwi", PUBLISH, strdup(v->value)); 
		} else if (!strcasecmp(v->name, "subscribe_mwi_event")) {
			res = set_event("mwi", SUBSCRIBE, strdup(v->value)); 
			
		} else if (!strcasecmp(v->name, "devicestate_prefix")) {
			res = set_event("device_state", PREFIX, strdup(v->value)); 
		} else if (!strcasecmp(v->name, "publish_devicestate_event")) {
			res = set_event("device_state", PUBLISH, strdup(v->value)); 
		} else if (!strcasecmp(v->name, "subscribe_devicestate_event")) {
			res = set_event("device_state", SUBSCRIBE, strdup(v->value)); 
			
		} else if (!strcasecmp(v->name, "devicestate_change_prefix")) {
			res = set_event("device_state_change", PREFIX, strdup(v->value)); 
		} else if (!strcasecmp(v->name, "publish_devicestate_change_event")) {
			res = set_event("device_state_change", PUBLISH, strdup(v->value)); 
		} else if (!strcasecmp(v->name, "subscribe_devicestate_change_event")) {
			res = set_event("device_state_change", SUBSCRIBE, strdup(v->value));
		} else {
			ast_log(LOG_WARNING, "Unknown option '%s'\n", v->name);
		}
	}
	ast_rwlock_unlock(&event_types_lock);
	if (!servers) {
		servers = strdup(default_servers);
	}
	AST_LOG_NOTICE_DEBUG("Done loading config\n");

	return res;
}

static int load_config(unsigned int reload)
{
	static const char filename[] = "res_redis.conf";
	struct ast_config *cfg;
	const char *cat = NULL;
	struct ast_flags config_flags = { 0 };
	int res = 0;

	cfg = ast_config_load(filename, config_flags);

	if (cfg == CONFIG_STATUS_FILEMISSING || cfg == CONFIG_STATUS_FILEINVALID) {
		return -1;
	}

	while ((cat = ast_category_browse(cfg, cat))) {
		if (!strcasecmp(cat, "general")) {
			res = load_general_config(cfg);
		} else {
			ast_log(LOG_WARNING, "Unknown configuration section '%s'\n", cat);
		}
	}

	ast_config_destroy(cfg);

	return res;
}

static void *dispatch_thread_handler(void *data)
{
	struct event_base *eventbase = data;

	redisLibeventAttach(redisPubConn,eventbase);
	redisLibeventAttach(redisSubConn,eventbase);
	
	redisAsyncSetConnectCallback(redisPubConn, redis_connect_cb);
	redisAsyncSetDisconnectCallback(redisPubConn, redis_disconnect_cb);
	
	redisAsyncSetConnectCallback(redisSubConn, redis_connect_cb);
	redisAsyncSetDisconnectCallback(redisSubConn, redis_disconnect_cb);
	
	event_base_dispatch(eventbase);
	return NULL;
}


static void cleanup_module(void)
{
	unsigned int i = 0;
	for (i = 0; i < ARRAY_LEN(event_types); i++) {
		if (event_types[i].sub) {
#ifdef HAVE_PBX_STASIS_H
			event_types[i].sub = stasis_unsubscribe(event_types[i].sub);
#else
			event_types[i].sub = ast_event_unsubscribe(event_types[i].sub);
#endif
		}
		event_types[i].publish = 0;
		event_types[i].subscribe = 0;
		if (event_types[i].channelstr) {
			ast_free(event_types[i].channelstr);
			event_types[i].channelstr = NULL;
		}
		if (event_types[i].prefix) {
			ast_free(event_types[i].prefix);
			event_types[i].prefix = NULL;
		}
	}

	ast_mutex_lock(&redis_lock);
	stoprunning = 1;
	event_base_loopbreak(eventbase);
	ast_mutex_unlock(&redis_lock);

	if (dispatch_thread_id != AST_PTHREADT_NULL) {
		pthread_kill(dispatch_thread_id, SIGURG);
		pthread_join(dispatch_thread_id, NULL);
	}
	
	if (servers) {
		ast_free(servers);
		servers = NULL;
	}
}

static int load_module(void)
{
	enum ast_module_load_result res = AST_MODULE_LOAD_FAILURE;
	unsigned int i = 0;

	AST_LOG_NOTICE_DEBUG("Loading res_config_redis...\n");
	ast_debug(1, "Loading res_config_redis...\n");

        ast_eid_to_str(default_eid_str, sizeof(default_eid_str), &ast_eid_default);
	if (load_config(0)) {
		// simply not configured is not a fatal error
		ast_log(LOG_ERROR, "Declining load of the module, until config issue is resolved\n");
		res = AST_MODULE_LOAD_DECLINE;
		return res;
		//goto failed;
	}

	ast_cli_register_multiple(redis_cli, ARRAY_LEN(redis_cli));

	/* create libevent base */
	eventbase = event_base_new();

	/* connect to the first available redis server */
	if (!redis_connect_nextserver()) {
		if (redisPubConn->err) {
			ast_log(LOG_ERROR, "connecting to any of the redis servers failed: error: %s..\n", redisPubConn->errstr);
		}
		if (redisSubConn->err) {
			ast_log(LOG_ERROR, "connecting to any of the redis servers failed: error: %s..\n", redisSubConn->errstr);
		}
		goto failed;
	}

	if (ast_pthread_create_background(&dispatch_thread_id, NULL, dispatch_thread_handler, eventbase)) {
		ast_log(LOG_ERROR, "Error starting Redis dispatch thread.\n");
		goto failed;
	}

#ifdef HAVE_PBX_STASIS_H
#else
	ast_enable_distributed_devstate();
#endif	
	redis_dump_ast_event_cache();

	ast_rwlock_wrlock(&event_types_lock);
	for (i = 0; i < ARRAY_LEN(event_types); i++) {
		if (!event_types[i].publish) {
			ast_debug(1, "Skipping '%s' not published\n", event_types[i].channelstr);
			continue;
		}
		if (event_types[i].publish && !event_types[i].sub) {
#ifdef HAVE_PBX_STASIS_H
			event_types[i].sub = stasis_subscribe(ast_device_state_topic_all(), ast_event_cb, NULL);
#else
			event_types[i].sub = ast_event_subscribe(i, ast_event_cb, "res_redis", NULL, AST_EVENT_IE_END);
#endif
		}
		AST_LOG_NOTICE_DEBUG("Subscribing to redis channel '%s'\n", event_types[i].channelstr);
 		redisAsyncCommand(redisSubConn, redis_subscription_cb, NULL, "SUBSCRIBE %s", event_types[i].channelstr);
		if (redisSubConn->err) {
			ast_log(LOG_ERROR, "redisAsyncCommand Send error: %s\n", redisSubConn->errstr);
		}
	}
	ast_rwlock_unlock(&event_types_lock);
	AST_LOG_NOTICE_DEBUG("res_redis loaded\n");
	
	return AST_MODULE_LOAD_SUCCESS;

failed:
	cleanup_module();
	return res;
}

static int unload_module(void)
{
	ast_debug(1, "Unloading res_config_redis...\n");
	ast_cli_unregister_multiple(redis_cli, ARRAY_LEN(redis_cli));

	cleanup_module();
	ast_debug(1, "Done Unloading res_config_redis...\n");
	return 0;
}

static int reload(void)
{
	ast_debug(1, "Reloading res_redis not implemented yet!...\n");
	enum ast_module_load_result res = AST_MODULE_LOAD_DECLINE;

/*
	enum ast_module_load_result res = AST_MODULE_LOAD_FAILURE;
	unsigned int i = 0;

	ast_debug(1, "Reloading res_config_redis...\n");
	cleanup_module();
	if (load_config(1)) {
		// simply not configured is not a fatal error
		res = AST_MODULE_LOAD_DECLINE;
		goto failed;
	}
	redisAsyncDisconnect(redisSubConn);
	redisAsyncDisconnect(redisPubConn);
	
	if (!redis_connect_nextserver()) {
		if (redisPubConn->err) {
			ast_log(LOG_ERROR, "connecting to any of the redis servers failed: error: %s..\n", redisPubConn->errstr);
		}
		if (redisSubConn->err) {
			ast_log(LOG_ERROR, "connecting to any of the redis servers failed: error: %s..\n", redisSubConn->errstr);
		}
		goto failed;
	}
	redis_dump_ast_event_cache();

	ast_rwlock_wrlock(&event_types_lock);
	for (i = 0; i < ARRAY_LEN(event_types); i++) {
		if (!event_types[i].publish) {
			ast_debug(1, "Skipping '%s' not published\n", event_types[i].channelstr);
			continue;
		}
		if (event_types[i].publish && !event_types[i].sub) {
			event_types[i].sub = ast_event_subscribe(i, ast_event_cb, "res_redis", NULL, AST_EVENT_IE_END);
		}
		AST_LOG_NOTICE_DEBUG("Subscribing to redis channel '%s'\n", event_types[i].channelstr);
 		redisAsyncCommand(redisSubConn, redis_subscription_cb, NULL, "SUBSCRIBE %s", event_types[i].channelstr);
		if (redisSubConn->err) {
			ast_log(LOG_ERROR, "redisAsyncCommand Send error: %s\n", redisSubConn->errstr);
		}
	}
	ast_rwlock_unlock(&event_types_lock);

	ast_debug(1, "Done Reloading res_config_redis...\n");
	return AST_MODULE_LOAD_SUCCESS;
failed:
	cleanup_module();
*/
	return res;
}

void _log_console(int level, const char *file, int line, const char *function, const char *fmt, ...)
{
        va_list ap;
        va_start(ap, fmt);
	ast_log(level, file, line, function, fmt, ap);
	va_end(ap);
}
void _log_debug(int level, const char *file, int line, const char *function, const char *fmt, ...)
{
        va_list ap;
        va_start(ap, fmt);
	ast_debug(level, file, line, function, fmt, ap);
	va_end(ap);
}
void _log_verbose(int level, const char *file, int line, const char *function, const char *fmt, ...)
{
        va_list ap;
        va_start(ap, fmt);
	__ast_verbose(file, line, function, level, fmt, ap);
	va_end(ap);
}


//AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Redis");
AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Redis RealTime PubSub Driver",
	.load = load_module,
	.unload = unload_module,
	.reload = reload,
	.load_pri = AST_MODPRI_REALTIME_DRIVER,
);
