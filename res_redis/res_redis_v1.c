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

#define AST_MODULE "res_redis_v1"

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


#include "../include/pbx_event_message_serializer.h"
#include "../include/message_queue_pubsub.h"
#include "../include/shared.h"

/*
 * declarations
 */
static char *redis_show_config(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char *redis_ping(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static struct ast_cli_entry redis_cli[] = {
	AST_CLI_DEFINE(redis_show_config, "Show configuration"),
	AST_CLI_DEFINE(redis_ping, "Send a test ping to the cluster"),
};
 
/*
 * globals 
 */
//AST_RWLOCK_DEFINE_STATIC(event_types_lock);
AST_MUTEX_DEFINE_STATIC(reload_lock);
static char *default_eid_str;

static void cleanup_module(void)
{
	log_verbose(2, "res_redis: Enter (%s)\n", __PRETTY_FUNCTION__);
	exception_t res = NO_EXCEPTION;

	// cleanup first;
	msq_remove_all_servers();
	
	log_verbose(2, "res_redis: Exit %s%s\n", res ? ", Exception Occured: " : "", res ? exception2str[res].str : "");
}

static int load_general_config(struct ast_config *cfg)
{
	struct ast_variable *v;
	int res = 0;
	ast_debug(2,"Loading config: [general] section\n");

	for (v = ast_variable_browse(cfg, "general"); v && !res; v = v->next) {
		if (!strcasecmp(v->name, "server")) {
			ast_debug(4, "Add Server: %s\n", v->value);
			if (v->value[0] ==  '/') {
				res |= msq_add_server(NULL, 0, v->value);
			} else {
				char *valuestr = strdupa(v->value);
				char *url = strtok(valuestr, ":");;
				int port = atoi(valuestr);
				res |= msq_add_server(url, port, NULL);
			}
			ast_debug(4,"Server %s Added\n", v->value);
		} else {
			ast_log(LOG_WARNING, "Unknown option '%s'\n", v->name);
		}
	}
	//msq_list_servers();
	ast_debug(2,"Done loading config: [general] section\n");
	return res;
}

static int load_channel_config(struct ast_config *cfg, const char *cat)
{
	struct ast_variable *v;
	int res = 0;
	ast_debug(2,"Loading loading category [%s]\n", cat);

	for (v = ast_variable_browse(cfg, cat); v && !res; v = v->next) {
		if (!strcasecmp(v->name, "publish")) {
			res |= 0;
		} else if (!strcasecmp(v->name, "subscribe")) {
			res |= 0;
		} else if (!strcasecmp(v->name, "channel")) {
			res |= 0;
		} else if (!strcasecmp(v->name, "device_prefix")) {
			res |= 0;
		} else if (!strcasecmp(v->name, "dump_state_table_on_connection")) {
			res |= 0;
		} else {
			ast_log(LOG_WARNING, "Unknown option '%s'\n", v->name);
			//res = 1;
		}
	}
	ast_debug(2,"Done loading category [%s]\n", cat);
	return res;
}

static int load_config(unsigned int reload)
{
	static const char filename[] = "res_redis_v1.conf";
	log_verbose(2, "res_redis: Enter (%s)\n", __PRETTY_FUNCTION__);
	struct ast_config *cfg;
	const char *cat = NULL;
	struct ast_flags config_flags = { 0 };
	int res = 0;

	cfg = ast_config_load(filename, config_flags);

	if (cfg == CONFIG_STATUS_FILEMISSING || cfg == CONFIG_STATUS_FILEINVALID) {
		return -1;
	}
	
	if (reload) {
		cleanup_module();
	}

	while ((cat = ast_category_browse(cfg, cat))) {
		if (!strcasecmp(cat, "general")) {
			res = load_general_config(cfg);
		} else {
			res = load_channel_config(cfg, cat);
		}
	}

	ast_config_destroy(cfg);

	return res;
}

static int load_module(void)
{
	enum ast_module_load_result res = AST_MODULE_LOAD_FAILURE;
	ast_log(LOG_NOTICE,"Loading res_config_redis...\n");

        ast_eid_to_str(default_eid_str, sizeof(default_eid_str), &ast_eid_default);
	if (load_config(0)) {
		ast_log(LOG_ERROR,"Declining load of the module, until config issue is resolved\n");
		res = AST_MODULE_LOAD_DECLINE;
		goto failed;
	}
	
	// start libevent loop
	
	// dump currently cached events
	
	// subscribe to channels

	ast_cli_register_multiple(redis_cli, ARRAY_LEN(redis_cli));
	ast_enable_distributed_devstate();

	ast_log(LOG_NOTICE,"res_redis loaded\n");
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
	enum ast_module_load_result res = AST_MODULE_LOAD_DECLINE;
	ast_debug(1, "Reloading res_redis not implemented yet!...\n");
	ast_mutex_lock(&reload_lock);
	load_config(1);
	ast_mutex_unlock(&reload_lock);
	goto failed;
	
failed:
	cleanup_module();
	return res;
}

void _log_verbose(int level, const char *file, int line, const char *function, const char *fmt, ...)
{
        va_list ap;
        va_start(ap, fmt);
	if (level >= 4) {
		__ast_verbose_ap(file, line, function, level, NULL, fmt, ap);
	} else {
		__ast_verbose_ap(file, line, function, level, NULL, fmt, ap);
	}
	va_end(ap);
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


//AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Redis");
AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Redis RealTime PubSub Driver",
	.load = load_module,
	.unload = unload_module,
	.reload = reload,
	.load_pri = AST_MODPRI_REALTIME_DRIVER,
);
