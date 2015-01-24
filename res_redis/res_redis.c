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


#include <asterisk.h>

#define AST_MODULE "res_redis"

ASTERISK_FILE_VERSION(__FILE__, "$Revision: 419592 $")

#include <hiredis/hiredis.h>
#include <hiredis/async.h>
#include <hiredis/adapters/libevent.h>

#include "asterisk/module.h"
#include "asterisk/logger.h"
#include "asterisk/config.h"
#include "asterisk/event.h"
#include "asterisk/cli.h"
#include "asterisk/netsock2.h"
#include "asterisk/devicestate.h"
//#include "asterisk/strings.h"
#include "asterisk/event.h"

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
char default_eid_str[32];

/* predeclarations */
static void ast_event_cb(const struct ast_event *event, void *data);
static void redis_dump_ast_event_cache();
static int redis_decode_msg2mwi(struct ast_event **eventref, const char *msg);
static int redis_encode_mwi2msg(char *msg, const size_t msg_len, const struct ast_event *event);
static int redis_decode_msg2devicestate(struct ast_event **eventref, const char *msg);
static int redis_encode_devicestate2msg(char *msg, const size_t msg_len, const struct ast_event *event);
static void redis_subscription_cb(redisAsyncContext *c, void *r, void *privdata);

static struct event_type {
	const char *name;
	struct ast_event_sub *sub;
	unsigned char publish;
	unsigned char subscribe;
	unsigned char publish_default;
	unsigned char subscribe_default;
	char *channelstr;
	char *prefix;
	void (*redis_subscription_cb) (redisAsyncContext *c, void *r, void *privdata);
	int (*encode_event2msg) (char *msg, const size_t msg_len, const struct ast_event *event);
	int (*decode_msg2event) (struct ast_event **eventref, const char *msg);
} event_types[] = {
	[AST_EVENT_MWI] = { .name = "mwi", .redis_subscription_cb = redis_subscription_cb, .encode_event2msg = redis_encode_mwi2msg, .decode_msg2event = redis_decode_msg2mwi},
	[AST_EVENT_DEVICE_STATE_CHANGE] = { .name = "device_state", .redis_subscription_cb = redis_subscription_cb, .encode_event2msg = redis_encode_devicestate2msg, .decode_msg2event = redis_decode_msg2devicestate},
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
		ast_log(LOG_NOTICE, "res_redis: Connecting to curserver:'%s', server:'%s' / servers:'%s' / remaining:'%s'\n", curserver, server, servers, remaining);
		if (server[0] == '/') {
			ast_log(LOG_NOTICE, "res_redis: Use Socket: %s\n", server);
			redisPubConn = redisAsyncConnectUnix(server);
			redisSubConn = redisAsyncConnectUnix(server);
		} else {
			ast_sockaddr_split_hostport(server, &host, &portstr, 0);
			if (!ast_strlen_zero(portstr)) {
				port = atoi(portstr);
			}
			ast_log(LOG_NOTICE, "res_redis: Use Server: '%s', Port: '%d'\n", server, port);
			if (!ast_strlen_zero(server)) {
				redisPubConn = redisAsyncConnect(server, port);
				redisSubConn = redisAsyncConnect(server, port);
			}
		}
		if (redisPubConn == NULL || redisPubConn->err || redisSubConn == NULL || redisSubConn->err) {
			if (redisPubConn || redisSubConn) {
				ast_log(LOG_ERROR, "res_redis: Connection error: %s / %s\n", redisPubConn->errstr, redisSubConn->errstr);
			} else {
				ast_log(LOG_ERROR, "res_redis: Connection error: Can't allocated redis context\n");
			}
			if (servers) {
				curserver = strtok_r(NULL, delims, &remaining);
				continue;
			} else {
				return 0;
			}
		}
		ast_log(LOG_NOTICE, "res_redis: Async Connection Started %s\n", curserver);
		break;
	}
	redis_dump_ast_event_cache();
	return -1;
}


enum {
	DECODING_ERROR,
	EID_SELF,
	OK,
	OK_CACHABLE,
};

void redis_pong_cb(redisAsyncContext *c, void *r, void *privdata) {
	redisReply *reply = r;
	if (reply == NULL) {
		return;
	}
	ast_log(LOG_NOTICE, "res_redis: Pong\n");
}

void redis_meet_cb(redisAsyncContext *c, void *r, void *privdata) {
	redisReply *reply = r;
	if (reply == NULL) {
		return;
	}
	ast_log(LOG_NOTICE, "res_redis: Meet\n");
}
static int redis_decode_msg2mwi(struct ast_event **eventref, const char *msg)
{
	static const char decode_format[] = "{eid_str:%[^,], mailbox:%[^,], context:%[^,], newmsgs:%u, oldmsgs:%u}";
	char eid_str[32];
	char mailbox[50];
	char context[50];
	unsigned int newmsgs;
	unsigned int oldmsgs;
	struct ast_event *event = *eventref;
	
	if (sscanf(msg, decode_format, eid_str, mailbox, context, &newmsgs, &oldmsgs)) {
		ast_log(LOG_NOTICE, "res_redis: Received: eid: %s, mailbox:%s, context:%s, newmsgs:%u, oldmsgs:%u\n", eid_str, mailbox, context, newmsgs, oldmsgs);
		struct ast_eid eid;
		ast_str_to_eid(&eid, eid_str);
		if (!ast_eid_cmp(&ast_eid_default, &eid)) {
			/* Don't feed events back in that originated locally. */
			return EID_SELF;
		}
		if ((event = ast_event_new(AST_EVENT_MWI,
				AST_EVENT_IE_MAILBOX, AST_EVENT_IE_PLTYPE_STR, mailbox,
			        AST_EVENT_IE_CONTEXT, AST_EVENT_IE_PLTYPE_STR, context,
			        AST_EVENT_IE_OLDMSGS, AST_EVENT_IE_PLTYPE_UINT, oldmsgs,
			        AST_EVENT_IE_NEWMSGS, AST_EVENT_IE_PLTYPE_UINT, newmsgs,
			        AST_EVENT_IE_EID, AST_EVENT_IE_PLTYPE_RAW, &eid, sizeof(eid),
				AST_EVENT_IE_END))
		) {
			ast_log(LOG_NOTICE, "res_redis: Sent devicestate to asterisk'\n");
			*eventref = event;
			return OK_CACHABLE;
		} else {
			ast_log(LOG_ERROR, "res_redis: Could not create mwi ast_event'\n");
		}
	} else {
		ast_log(LOG_ERROR, "res_redis, event received from redis could not be decoded: '%s'\n", msg);
	}
	return DECODING_ERROR;
}

static int redis_encode_mwi2msg(char *msg, const size_t msg_len, const struct ast_event *event)
{
	static const char encode_format[] = "{eid_str:%s, mailbox:%s, context:%s, newmsgs:%u, oldmsgs:%u}";
	char outbuffer[MAX_EVENT_LENGTH] = "";
        char eid_str[32];

        if (ast_eid_cmp(&ast_eid_default, ast_event_get_ie_raw(event, AST_EVENT_IE_EID)))
        {
                // If the event didn't originate from this server, don't send it back out.
                ast_debug(1, "Returning here\n");
                return 0;
        }
 
        ast_eid_to_str(eid_str, sizeof(eid_str), &ast_eid_default);
	snprintf(outbuffer, MAX_EVENT_LENGTH, encode_format, 
		eid_str, 
		ast_event_get_ie_str(event, AST_EVENT_IE_MAILBOX),
		ast_event_get_ie_str(event, AST_EVENT_IE_CONTEXT),
		ast_event_get_ie_uint(event, AST_EVENT_IE_NEWMSGS),
		ast_event_get_ie_uint(event, AST_EVENT_IE_OLDMSGS));

	return 1;
}

static int redis_decode_msg2devicestate(struct ast_event **eventref, const char *msg)
{
	static const char decode_format[] = "{eid_str:%[^,],device:%[^,],state:%u,statestr:%[^,],cacheable:%u}";
	char eid_str[32];
	char device[50];
	unsigned int device_state;
	char state_str[30];
	unsigned int cachable;
	struct ast_event *event = *eventref;
	
	if (sscanf(msg, decode_format, eid_str, device, &device_state, state_str, &cachable)) {
		ast_log(LOG_NOTICE, "res_redis: Received: eid: %s, device:%s, device_state:%u, cachable:%u\n", eid_str, device, device_state, cachable);
		struct ast_eid eid;
		ast_str_to_eid(&eid, eid_str);
		if (!ast_eid_cmp(&ast_eid_default, &eid)) {
			/* Don't feed events back in that originated locally. */
			return EID_SELF;
		}
		if ((event = ast_event_new(AST_EVENT_DEVICE_STATE_CHANGE,
				AST_EVENT_IE_DEVICE, AST_EVENT_IE_PLTYPE_STR, device, 
				AST_EVENT_IE_STATE, AST_EVENT_IE_PLTYPE_UINT, device_state,
			        AST_EVENT_IE_EID, AST_EVENT_IE_PLTYPE_RAW, &eid, sizeof(eid),
				AST_EVENT_IE_CACHABLE, AST_EVENT_IE_PLTYPE_UINT, cachable,
				AST_EVENT_IE_END))
		) {
/*
			ast_log(LOG_NOTICE, "res_redis: event: device: '%s'\n", ast_event_get_ie_str(event, AST_EVENT_IE_DEVICE));
			ast_log(LOG_NOTICE, "res_redis: event: state: '%u'\n", ast_event_get_ie_uint(event, AST_EVENT_IE_STATE));
			ast_log(LOG_NOTICE, "res_redis: event: device: '%u'\n", ast_event_get_ie_uint(event, AST_EVENT_IE_CACHABLE));
			char eid_str1[32];
			const void *eid = ast_event_get_ie_raw(event, AST_EVENT_IE_EID);
		        ast_eid_to_str(eid_str1, sizeof(eid_str1), (void *)eid);
			ast_log(LOG_NOTICE, "res_redis: event: eid:  '%s'\n", eid_str1);
			char *tmp = ast_alloca(MAX_EVENT_LENGTH);
			redis_encode_devicestate2msg(tmp, MAX_EVENT_LENGTH, event);
			ast_log(LOG_NOTICE, "Check Decoded Event: '%s'\n", tmp);
*/
			*eventref = event;
			return OK + cachable;
		} else {
			ast_log(LOG_ERROR, "res_redis: Could not create Send devicestate to asterisk'\n");
		}
	} else {
		ast_log(LOG_ERROR, "res_redis, event received from redis could not be decoded: '%s'\n", msg);
	}
	return DECODING_ERROR;
}

static int redis_encode_devicestate2msg(char *msg, const size_t msg_len, const struct ast_event *event)
{
	static const char encode_format[] = "{eid_str:%s,device:%s,state:%u,statestr:%s,cacheable:%u}";
        char eid_str[32];

        ast_eid_to_str(eid_str, sizeof(eid_str), &ast_eid_default);
	snprintf(msg, msg_len, encode_format, 
		eid_str, 
		ast_event_get_ie_str(event, AST_EVENT_IE_DEVICE), 
		ast_event_get_ie_uint(event, AST_EVENT_IE_STATE), 
		ast_devstate_str(ast_event_get_ie_uint(event, AST_EVENT_IE_STATE)), 
		ast_event_get_ie_uint(event, AST_EVENT_IE_CACHABLE));

	return 1;
}

static void redis_subscription_cb(redisAsyncContext *c, void *r, void *privdata) {
	int j;
	redisReply *reply = r;
	if (reply == NULL) {
		return;
	}
	if (reply->type == REDIS_REPLY_ARRAY) {
/*		for (j = 0; j < reply->elements; j++) {
			ast_log(LOG_NOTICE, "res_redis: REDIS_SUBSCRIPTION_CB: [%u]: %s\n", j, reply->element[j]->str);
		}*/
		if (!strcasecmp(reply->element[0]->str, "MESSAGE")) {
			if (!ast_strlen_zero(reply->element[1]->str) && !ast_strlen_zero(reply->element[2]->str)) {
				ast_log(LOG_NOTICE, "res_redis: start decoding'\n");
				struct event_type *etype;
				
				ast_rwlock_rdlock(&event_types_lock);
				etype = &event_types[AST_EVENT_DEVICE_STATE_CHANGE];
				ast_rwlock_unlock(&event_types_lock);
				
				if (etype) {
					if (etype->publish) {
						if (!strcasecmp(reply->element[1]->str, etype->channelstr)) {
							struct ast_event *event = NULL;
							char *msg = ast_strdupa(reply->element[2]->str);
							unsigned int res = 0;
							
							if (strlen(reply->element[2]->str) < ast_event_minimum_length()) {
								ast_log(LOG_ERROR, "Ignoring event that's too small. %u < %u\n", (unsigned int) strlen(reply->element[2]->str), (unsigned int) ast_event_minimum_length());
								return;
							}

							if ((res = etype->decode_msg2event(&event, msg))) {
								if (res == EID_SELF) {
									// skip feeding back to self
									// ast_log(LOG_NOTICE, "res_redis: Originated Here. skip'\n");
									ast_event_destroy(event);
									return;
								} else {
									/*
									// check decoding
									char eid_str1[32];
									const void *eid = ast_event_get_ie_raw(event, AST_EVENT_IE_EID);
									if (eid) {
										ast_eid_to_str(eid_str1, sizeof(eid_str1), (void *)eid);
									}
									ast_log(LOG_NOTICE, "res_redis: event: device: '%s'\n", ast_event_get_ie_str(event, AST_EVENT_IE_DEVICE));
									ast_log(LOG_NOTICE, "res_redis: event: state: '%u'\n", ast_event_get_ie_uint(event, AST_EVENT_IE_STATE));
									ast_log(LOG_NOTICE, "res_redis: event: device: '%u'\n", ast_event_get_ie_uint(event, AST_EVENT_IE_CACHABLE));
									ast_log(LOG_NOTICE, "res_redis: event: eid: '%s' / %p\n", eid_str1, eid);
									*/
									if (res == OK) {
										ast_event_queue(event);
									}
									if (res == OK_CACHABLE) {
										ast_event_queue_and_cache(event);
									}
									ast_log(LOG_NOTICE, "res_redis: ast_event sent'\n");
								}
							} else {
								ast_log(LOG_NOTICE, "res_redis: error decoding %s'\n", msg);
								ast_event_destroy(event);
							}
						} else {
							ast_log(LOG_NOTICE, "res_redis: has different channelstr '%s'\n", etype->channelstr);
						}
					} else {
						ast_log(LOG_NOTICE, "res_redis: event_type should not be published'\n");
					}
				} else {
					ast_log(LOG_NOTICE, "res_redis: event_type does not exist'\n");
				}
			}
		} else {
			for (j = 0; j < reply->elements; j++) {
				ast_log(LOG_NOTICE, "res_redis: REDIS_SUBSCRIPTION_CB: [%u]: %s\n", j, reply->element[j]->str);
			}
		}
	}
}

void redis_connect_cb(const redisAsyncContext *c, int status) {
	if (status != REDIS_OK) {
		printf("Error: %s\n", c->errstr);
		return;
	}
	ast_log(LOG_NOTICE, "res_redis: Connected\n");
}

void redis_disconnect_cb(const redisAsyncContext *c, int status) {
	if (status != REDIS_OK) {
		printf("Error: %s\n", c->errstr);
		return;
	}
	ast_log(LOG_NOTICE, "res_redis: Disconnected\n");
	ast_mutex_lock(&redis_lock);
	if (!stoprunning) {
		redis_connect_nextserver();
	}
	ast_mutex_unlock(&redis_lock);
}


static void redis_dump_ast_event_cache()
{
	if (dispatch_thread_id != AST_PTHREADT_NULL) {
		ast_log(LOG_NOTICE, "res_redis: Dumping Ast Event Cache to %s\n", curserver);
		unsigned int i = 0;
		// flush all changes
		for (i = 0; i < ARRAY_LEN(event_types); i++) {
			struct ast_event_sub *event_sub;
			ast_rwlock_rdlock(&event_types_lock);
			if (!event_types[i].publish) {
				ast_rwlock_unlock(&event_types_lock);
				ast_log(LOG_NOTICE, "res_redis: %s skipping not published\n", event_types[i].name);
				continue;
			}
			ast_rwlock_unlock(&event_types_lock);

			ast_log(LOG_NOTICE, "res_redis: subscribe %s\n", event_types[i].name);
			event_sub = ast_event_subscribe_new(i, ast_event_cb, NULL);
			ast_event_sub_append_ie_raw(event_sub, AST_EVENT_IE_EID, &ast_eid_default, sizeof(ast_eid_default));
			usleep(500);
			ast_log(LOG_NOTICE, "res_redis: Dumping Past %s Events\n", event_types[i].name);
			ast_event_dump_cache(event_sub);
			ast_event_sub_destroy(event_sub);
		}
		ast_log(LOG_NOTICE, "res_redis: Ast Event Cache Dumped to %s\n", curserver);
	}
}	

static void ast_event_cb(const struct ast_event *event, void *data)
{
	ast_log(LOG_NOTICE, "res_redis: ast_event_cb\n");

	const struct ast_eid *eid;
	char eid_str[32] = "";
	eid = ast_event_get_ie_raw(event, AST_EVENT_IE_EID);
	ast_eid_to_str(eid_str, sizeof(eid_str), (struct ast_eid *) eid);

	if (ast_event_get_type(event) == AST_EVENT_PING) {
		ast_log(LOG_NOTICE, "(ast_event_cb) Got event PING from server with EID: '%s' (Add Handler)\n", eid_str);
		/*
		if (ast_event_get_type(event) == AST_EVENT_PING) {
			const struct ast_eid *eid;
			char buf[128] = "";

			eid = ast_event_get_ie_raw(event, AST_EVENT_IE_EID);
			ast_eid_to_str(buf, sizeof(buf), (struct ast_eid *) eid);
			ast_log(LOG_NOTICE, "(ast_deliver_event) Got event PING from server with EID: '%s'\n", buf);

			ast_event_queue(event);
		*/
		redisAsyncCommand(redisPubConn, redis_meet_cb, NULL, "MEET");
		redisAsyncCommand(redisPubConn, redis_pong_cb, (char*)eid_str, "PING");
	}
	
	if (ast_eid_cmp(&ast_eid_default, eid)) {
		// If the event didn't originate from this server, don't send it back out.
		ast_log(LOG_NOTICE, "(ast_event_cb) didn't originate from this server, don't send it back out: (excep from :'%s')\n", eid_str);
		return;
	}
	
	// decode event2msg
	ast_log(LOG_NOTICE, "(ast_event_cb) decode incoming message\n");
	struct event_type *etype;
	char *msg = ast_alloca(MAX_EVENT_LENGTH + 1);
	
	ast_rwlock_rdlock(&event_types_lock);
	etype = &event_types[ast_event_get_type(event)];
	ast_rwlock_unlock(&event_types_lock);
	
	if (etype) {
		if (etype->publish) {
			if (etype->encode_event2msg(msg, MAX_EVENT_LENGTH, event)) {
				ast_log(LOG_NOTICE, "res_redis: sending 'PUBLISH %s %s'\n", etype->channelstr, msg);
				redisAsyncCommand(redisPubConn, NULL, NULL, "PUBLISH %s %s", etype->channelstr, msg);
			} else {
				ast_log(LOG_NOTICE, "res_redis: error encoding %s'\n", msg);
			}
		} else {
			ast_log(LOG_NOTICE, "res_redis: event_type should not be published'\n");
		}
	} else {
		ast_log(LOG_NOTICE, "res_redis: event_type does not exist'\n");
	}
}

static int set_event(const char *event_type, int pubsub, char *str)
{
	unsigned int i;

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

	ast_event_queue(event);

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
			//ast_free(servers);
			servers = strdup(v->value);
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
		} else {
			ast_log(LOG_WARNING, "Unknown option '%s'\n", v->name);
		}
	}
	ast_rwlock_unlock(&event_types_lock);
	if (!servers) {
		servers = strdup(default_servers);
	}

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
			event_types[i].sub = ast_event_unsubscribe(event_types[i].sub);
		}
		event_types[i].publish = 0;
		event_types[i].subscribe = 0;
		if (event_types[i].channelstr) {
			ast_free(event_types[i].channelstr);
		}
		if (event_types[i].prefix) {
			ast_free(event_types[i].prefix);
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
		servers = strdup(default_servers);
	}
}

static int load_module(void)
{
	enum ast_module_load_result res = AST_MODULE_LOAD_FAILURE;
	unsigned int i = 0;

	ast_log(LOG_NOTICE, "Loading res_config_redis...\n");
	ast_debug(1, "Loading res_config_redis...\n");

        ast_eid_to_str(default_eid_str, sizeof(default_eid_str), &ast_eid_default);
	if (load_config(0)) {
		// simply not configured is not a fatal error
		res = AST_MODULE_LOAD_DECLINE;
		goto failed;
	}

	ast_cli_register_multiple(redis_cli, ARRAY_LEN(redis_cli));

	/* create libevent base */
	eventbase = event_base_new();

	/* connect to the first available redis server */
	if (!redis_connect_nextserver()) {
		if (redisPubConn->err) {
			ast_log(LOG_ERROR, "res_redis: connecting to any of the redis servers failed: error: %s..\n", redisPubConn->errstr);
		}
		if (redisSubConn->err) {
			ast_log(LOG_ERROR, "res_redis: connecting to any of the redis servers failed: error: %s..\n", redisSubConn->errstr);
		}
		goto failed;
	}

	if (ast_pthread_create_background(&dispatch_thread_id, NULL, dispatch_thread_handler, eventbase)) {
		ast_log(LOG_ERROR, "Error starting Redis dispatch thread.\n");
		goto failed;
	}

	ast_enable_distributed_devstate();
	
	redis_dump_ast_event_cache();

	ast_rwlock_wrlock(&event_types_lock);
	for (i = 0; i < ARRAY_LEN(event_types); i++) {
		if (!event_types[i].publish) {
			ast_log(LOG_NOTICE, "res_redis: Skipping '%s' not published\n", event_types[i].channelstr);
			continue;
		}
		if (event_types[i].publish && !event_types[i].sub) {
			event_types[i].sub = ast_event_subscribe(i, ast_event_cb, "res_redis", NULL, AST_EVENT_IE_END);
		}
		ast_log(LOG_NOTICE, "res_redis: Subscribing to redis channel '%s'\n", event_types[i].channelstr);
 		redisAsyncCommand(redisSubConn, redis_subscription_cb, NULL, "SUBSCRIBE %s", event_types[i].channelstr);
	}
	ast_rwlock_unlock(&event_types_lock);
	
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
	if (load_config(0)) {
		// simply not configured is not a fatal error
		res = AST_MODULE_LOAD_DECLINE;
		goto failed;
	}
	if (!redis_connect_nextserver()) {
		if (redisPubConn->err) {
			ast_log(LOG_ERROR, "res_redis: connecting to any of the redis servers failed: error: %s..\n", redisPubConn->errstr);
		}
		if (redisSubConn->err) {
			ast_log(LOG_ERROR, "res_redis: connecting to any of the redis servers failed: error: %s..\n", redisSubConn->errstr);
		}
		goto failed;
	}
	redis_dump_ast_event_cache();

	ast_rwlock_wrlock(&event_types_lock);
	for (i = 0; i < ARRAY_LEN(event_types); i++) {
		if (!event_types[i].publish) {
			ast_log(LOG_NOTICE, "res_redis: Skipping '%s' not published\n", event_types[i].channelstr);
			continue;
		}
		if (event_types[i].publish && !event_types[i].sub) {
			event_types[i].sub = ast_event_subscribe(i, ast_event_cb, "res_redis", NULL, AST_EVENT_IE_END);
		}
		ast_log(LOG_NOTICE, "res_redis: Subscribing to redis channel '%s'\n", event_types[i].channelstr);
 		redisAsyncCommand(redisSubConn, redis_subscription_cb, NULL, "SUBSCRIBE %s", event_types[i].channelstr);
	}
	ast_rwlock_unlock(&event_types_lock);

	ast_debug(1, "Done Reloading res_config_redis...\n");
	return AST_MODULE_LOAD_SUCCESS;
failed:
	cleanup_module();
*/
	return res;
}


//AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Redis");
AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Redis RealTime PubSub Driver",
	.load = load_module,
	.unload = unload_module,
	.reload = reload,
	.load_pri = AST_MODPRI_REALTIME_DRIVER,
);
