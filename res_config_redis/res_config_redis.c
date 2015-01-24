#
#
#

#include <asterisk.h>

#define AST_MODULE "res_config_redis"

ASTERISK_FILE_VERSION(__FILE__, "$Revision: 419592 $")

#include <hiredis/hiredis.h>
//#include <hiredis/async.h>

#include <asterisk/file.h>
#include <asterisk/channel.h>
#include <asterisk/pbx.h>
#include <asterisk/config.h>
#include <asterisk/module.h>
#include <asterisk/lock.h>
#include <asterisk/utils.h>
#include <asterisk/cli.h>
#include <asterisk/threadstorage.h>

#include "config.h"
 
AST_MUTEX_DEFINE_STATIC(redis_lock);
AST_THREADSTORAGE(query_buf);
AST_THREADSTORAGE(result_buf);

#define RES_CONFIG_REDIS_CONF "res_config_redis.conf"

static redisContext *redisConn = NULL;

#define MAX_DB_OPTION_SIZE 64
static char hostname[MAX_DB_OPTION_SIZE] = "";
//static char dbuser[MAX_DB_OPTION_SIZE] = "";
//static char dbpass[MAX_DB_OPTION_SIZE] = "";
//static char dbname[MAX_DB_OPTION_SIZE] = "";
//static char dbappname[MAX_DB_OPTION_SIZE] = "";
//static char dbsock[MAX_DB_OPTION_SIZE] = "";
static int port = 6379;
struct timeval timeout = { 1, 500000 }; 	// 1.5 seconds

static int parse_config(int reload);
static int redis_reconnect(const char *database);
static char *handle_cli_realtime_redis_status(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);

static struct ast_cli_entry cli_realtime[] = {
	AST_CLI_DEFINE(handle_cli_realtime_redis_status, "Shows connection information for the Redis RealTime driver"),
};


#ifdef HAVE_PBX_VERSION_11
static struct ast_variable *realtime_redis(const char *database, const char *tablename, va_list ap)
#else
static struct ast_variable *realtime_redis(const char *database, const char *tablename, const struct ast_variable *fields)
#endif
{
	struct ast_variable *var = NULL;
	return var;
}

#ifdef HAVE_PBX_VERSION_11
static struct ast_config *realtime_multi_redis(const char *database, const char *table, va_list ap)
#else
static struct ast_config *realtime_multi_redis(const char *database, const char *table, const struct ast_variable *fields)
#endif
{
	struct ast_config *cfg = NULL;
	return cfg;
}

#ifdef HAVE_PBX_VERSION_11
static int update_redis(const char *database, const char *tablename, const char *keyfield,
						const char *lookup, va_list ap)
#else
static int update_redis(const char *database, const char *tablename, const char *keyfield,
						const char *lookup, const struct ast_variable *fields)
#endif
{
	return -1;
}

#ifdef HAVE_PBX_VERSION_11
static int update2_redis(const char *database, const char *tablename, va_list ap)
#else
static int update2_redis(const char *database, const char *tablename, const struct ast_variable *lookup_fields, const struct ast_variable *update_fields)
#endif
{
	return -1;
}

#ifdef HAVE_PBX_VERSION_11
static int store_redis(const char *database, const char *table, va_list ap)
#else
static int store_redis(const char *database, const char *table, const struct ast_variable *fields)
#endif
{
	ast_mutex_unlock(&redis_lock);
	if (!redis_reconnect(database)) {
		ast_mutex_unlock(&redis_lock);
		return -1;
	}
	return -1;
}

#ifdef HAVE_PBX_VERSION_11
static int destroy_redis(const char *database, const char *table, const char *keyfield, const char *lookup, va_list ap)
#else
static int destroy_redis(const char *database, const char *table, const char *keyfield, const char *lookup, const struct ast_variable *fields)
#endif
{
	ast_mutex_unlock(&redis_lock);
	if (!redis_reconnect(database)) {
		ast_mutex_unlock(&redis_lock);
		return -1;
	}
	return -1;
}

static struct ast_config *config_redis(const char *database, const char *table,
									   const char *file, struct ast_config *cfg,
									   struct ast_flags flags, const char *suggested_incl, const char *who_asked)
{
        // pre-check
	ast_mutex_lock(&redis_lock);
	// do load config
	ast_mutex_unlock(&redis_lock);
	return cfg;
}

static int require_redis(const char *database, const char *tablename, va_list ap)
{
	return -1;
}

static int unload_redis(const char *database, const char *tablename)
{
	return -1;
}

static struct ast_config_engine redis_engine = {
	.name = "redis",
	.load_func = config_redis,
	.realtime_func = realtime_redis,
	.realtime_multi_func = realtime_multi_redis,
	.store_func = store_redis,
	.destroy_func = destroy_redis,
	.update_func = update_redis,
	.update2_func = update2_redis,
	.require_func = require_redis,
	.unload_func = unload_redis,
};

static int load_module(void)
{
	ast_log(LOG_NOTICE, "Loading res_config_redis...\n");

	ast_debug(1, "Loading res_config_redis...\n");
//	if(!parse_config(0)) {
//		return AST_MODULE_LOAD_DECLINE;
//	}
	ast_config_engine_register(&redis_engine);

	ast_cli_register_multiple(cli_realtime, ARRAY_LEN(cli_realtime));

	ast_debug(1, "Done Loading res_config_redis...\n");
	return 0;
}

static int unload_module(void)
{
	ast_debug(1, "Unloading res_config_redis...\n");
	ast_mutex_lock(&redis_lock);
	if (redisConn) {
	        // disconnect
		redisConn = NULL;
	}
	ast_cli_unregister_multiple(cli_realtime, ARRAY_LEN(cli_realtime));
	ast_config_engine_deregister(&redis_engine);
	/* Unlock so something else can destroy the lock. */
	ast_mutex_unlock(&redis_lock);

	ast_debug(1, "Done Unloading res_config_redis...\n");
	return 0;
}

static int reload(void)
{
	ast_debug(1, "Reloading res_config_redis...\n");
	parse_config(1);
	ast_debug(1, "Done Reloading res_config_redis...\n");
	return 0;
}

static int parse_config(int is_reload)
{
	ast_debug(1, "Parsing Config '%s'...\n", RES_CONFIG_REDIS_CONF);
	ast_mutex_lock(&redis_lock);
	
	ast_mutex_unlock(&redis_lock);
	return 1;
}

static int redis_reconnect(const char *database)
{
	redisContext *conn = NULL;
	if (redisConn) {
		if (!redisConn->err) {
			// everything is still ok
			return -1;
		} else {
			// reconnect
			redisFree(redisConn);
		}
	}

	conn = redisConnectWithTimeout(hostname, port, timeout);
	
	if (conn == NULL || conn->err) {
		if (conn) {
	 		ast_log(LOG_ERROR, "Connection error: %s\n", conn->errstr);
		} else {
	 		ast_log(LOG_ERROR, "Connection error: Can't allocated redis context\n");
		}
		return 0;
	}
	redisConn = conn;
	return -1;
}

static char *handle_cli_realtime_redis_status(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	//char status[256];

	switch (cmd) {
		case CLI_INIT:
			e->command = "realtime show redis status";
			e->usage =
				"Usage: realtime show redis status\n"
				"       Shows connection information for the Redis RealTime driver\n";
			return NULL;
		case CLI_GENERATE:
			return NULL;
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
        }
        if (redisConn) {
		return CLI_SUCCESS;
	} else {
		return CLI_FAILURE;
	}
}

/* needs usecount semantics defined */
AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Redis RealTime Configuration Driver",
		.load = load_module,
		.unload = unload_module,
		.reload = reload,
		.load_pri = AST_MODPRI_REALTIME_DRIVER,
	       );
