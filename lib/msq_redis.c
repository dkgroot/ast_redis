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
#include "config.h"

#include "../include/message_queue_pubsub.h"

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <pthread.h>
#include <hiredis/hiredis.h>
#include <hiredis/async.h>
#include <hiredis/adapters/libevent.h>

#include "../include/shared.h"

/* 
 * declarations
 */
typedef struct msq_connection_map msq_connection_map_t;
exception_t processRedisAsyncError(redisAsyncContext *Conn);
/*
 * global
 */
pthread_rwlock_t msq_event_channel_map_rwlock = PTHREAD_RWLOCK_INITIALIZER;
struct msq_event_channel_map {
	const char *name;
	char *channel;
	char *pattern;
	msq_subscription_callback_t callback;
};
static msq_event_channel_t msq_event_channel_map[] = {
	[EVENT_MWI]			= {.name="mwi",},
	[EVENT_DEVICE_STATE]		= {.name="device_state",},
	[EVENT_DEVICE_STATE_CHANGE] 	= {.name="device_state_change"},
	[EVENT_PING]			= {.name="ping"}
};

pthread_rwlock_t msq_connection_map_rwlock = PTHREAD_RWLOCK_INITIALIZER;
struct msq_connection_map {
	const char *name;
	redisAsyncContext *Conn;
} msq_connection_map[] = {
	[PUBLISH] 			= {.name = "publish_server"},
	[SUBSCRIBE] 			= {.name = "subscribe_server"}
};

enum connection_type {
	SOCKET,
	URL
};

pthread_rwlock_t msq_server_rwlock = PTHREAD_RWLOCK_INITIALIZER;
typedef struct msq_servers {
	char *url;
	int port;
	char *socket;
	enum connection_type connection_type;
	struct msq_servers *next;
} server_t;
static server_t *servers_root = NULL;
static server_t *current_server = NULL;

/* RAII LOCK */
static inline void unlock_rwlock(pthread_rwlock_t **lock) 
{
	if (*lock) {
		pthread_rwlock_unlock(*lock);
	}
}
#define raii_rdlock(_x) {auto __attribute__((cleanup(unlock_rwlock))) pthread_rwlock_t *dtor = _x; pthread_rwlock_rdlock(_x);}
#define raii_wrlock(_x) {auto __attribute__((cleanup(unlock_rwlock))) pthread_rwlock_t *dtor = _x; pthread_rwlock_wrlock(_x);}
/* END RAII */

/*
 * public
 */
exception_t addserver(char *url, int port, char *socket)
{
	log_verbose(2, "Redis: Enter (%s)\n", __PRETTY_FUNCTION__);
	exception_t res = GENERAL_EXCEPTION;
	raii_wrlock(&msq_server_rwlock);
	server_t *server = servers_root;
	
	// check exists
	while (server && server->next != 0) {
		if ((!strcasecmp(server->url, url) && server->port == port) || !strcasecmp(server->socket, socket)) {
			// already there;
			LOG_ERROR_DEBUG("Redis: Server Already Added: url:'%s', port:%d, socket:'%s'\n", url, port, socket);
			return EXISTS_EXCEPTION;
		}
		server = server->next;
	}
	
	if (!server) {
		if (!(server = calloc(1, sizeof(server_t)))) {
			LOG_ERROR_DEBUG("Redis: Malloc Exception\n");
			res = MALLOC_EXCEPTION;
		}
		if (port && url && strlen(url) > 0) {
			server->url = strdup(url);
			server->port = port;
			current_server = server;
			server->connection_type = URL;
			res = NO_EXCEPTION;
		} else if (socket && strlen(socket) > 0) {
			server->socket = strdup(socket);
			server->connection_type = SOCKET;
			current_server = server;
			res = NO_EXCEPTION;
		}
		// else exception
	}
	log_verbose(2, "Redis: Exit %s%s\n", res ? ", Exception Occured: " : "", res ? exception2str[res].str : "");
	return res;
}

exception_t removeserver(char *url, int port, char *socket)
{
	log_verbose(2, "Redis: Enter (%s)\n", __PRETTY_FUNCTION__);
	exception_t res = GENERAL_EXCEPTION;
	raii_wrlock(&msq_server_rwlock);
	server_t *server = servers_root;
	server_t *prev_server = NULL;
	
	// check exists
	while (server && server->next != 0) {
		prev_server = server;
		if ((!strcasecmp(server->url, url) && server->port == port) || !strcasecmp(server->socket, socket)) {
			// found it
			prev_server->next = server->next;
			if (server->socket) {
				free(server->socket);
			}
			if (server->url) {
				free(server->url);
			}
			free(server);
			res = NO_EXCEPTION;
			break;
		}
		server = server->next;
	}
	log_verbose(2, "Redis: Exit %s%s\n", res ? ", Exception Occured: " : "", res ? exception2str[res].str : "");
	return res;
}

exception_t processRedisAsyncError(redisAsyncContext *Conn)
{
	log_verbose(2, "Redis: Enter (%s)\n", __PRETTY_FUNCTION__);
	if (Conn && Conn->err) {
		LOG_ERROR_DEBUG("Redis: Asynchronous Error: '%s'\n", Conn->errstr);
		return REDIS_EXCEPTION;
	}
	return NO_EXCEPTION;
}

exception_t connect_to_next_server()
{
	log_verbose(2, "Redis: Enter (%s)\n", __PRETTY_FUNCTION__);
	exception_t res = GENERAL_EXCEPTION;
	// check if connected
	disconnect();
		
	raii_rdlock(&msq_server_rwlock);
	server_t *server = current_server;
	if (server && server->next) {
		current_server = current_server->next;
	} else {
		current_server = servers_root;
	}
	
	raii_wrlock(&msq_connection_map_rwlock);
	switch (server->connection_type) {
		case SOCKET:
			msq_connection_map[PUBLISH].Conn = redisAsyncConnectUnix(server->socket);
			res |= processRedisAsyncError(msq_connection_map[PUBLISH].Conn);
			msq_connection_map[SUBSCRIBE].Conn = redisAsyncConnectUnix(server->socket);
			res |= processRedisAsyncError(msq_connection_map[SUBSCRIBE].Conn);
			break;
		case URL:
			msq_connection_map[PUBLISH].Conn = redisAsyncConnect(server->url, server->port);
			res |= processRedisAsyncError(msq_connection_map[PUBLISH].Conn);
			msq_connection_map[SUBSCRIBE].Conn = redisAsyncConnect(server->url, server->port);
			res |= processRedisAsyncError(msq_connection_map[SUBSCRIBE].Conn);
			break;
	}
	
	log_verbose(2, "Redis: Exit %s%s\n", res ? ", Exception Occured: " : "", res ? exception2str[res].str : "");
	return res;
}

exception_t disconnect()
{
	log_verbose(2, "Redis: Enter (%s)\n", __PRETTY_FUNCTION__);
	exception_t res = NO_EXCEPTION;
	raii_wrlock(&msq_connection_map_rwlock);
	
	if (msq_connection_map[PUBLISH].Conn) {
		// call general unsubscribe
		redisAsyncDisconnect(msq_connection_map[PUBLISH].Conn);
		msq_connection_map[PUBLISH].Conn = NULL;
	}
	if (msq_connection_map[SUBSCRIBE].Conn) {
		// call general unsubscribe
		redisAsyncDisconnect(msq_connection_map[SUBSCRIBE].Conn);
		msq_connection_map[SUBSCRIBE].Conn = NULL;
	}
	log_verbose(2, "Redis: Exit %s%s\n", res ? ", Exception Occured: " : "", res ? exception2str[res].str : "");
	return res;
}

/*
struct msq_event_channel_map {
	const char *name;
	char *channel;
	char *pattern;
	msq_subscription_callback_t callback;
};
static msq_event_channel_t event_channel_map[] = {
	[EVENT_MWI]			= {.name="mwi",},
	[EVENT_DEVICE_STATE]		= {.name="device_state",},
	[EVENT_DEVICE_STATE_CHANGE] 	= {.name="device_state_change"},
	[EVENT_PING]			= {.name="ping"}
};
*/

exception_t publish(event_type_t channel, char *publishmsg)
{
	log_verbose(2, "Redis: Enter (%s)\n", __PRETTY_FUNCTION__);
	exception_t res = NO_EXCEPTION;
	raii_rdlock(&msq_event_channel_map_rwlock);
	if (msq_event_channel_map[channel].channel) {
		LOG_NOTICE_DEBUG("Redis: PUBLISH channel: '%s', mesg: '%s'\n", msq_event_channel_map[channel].channel, publishmsg);
		redisAsyncCommand(msq_connection_map[PUBLISH].Conn, NULL, NULL, "PUBLISH %s %b", msq_event_channel_map[channel].channel, publishmsg, (size_t)strlen(publishmsg));
		res |= processRedisAsyncError(msq_connection_map[SUBSCRIBE].Conn);
	} 
	log_verbose(2, "Redis: Exit %s%s\n", res ? ", Exception Occured: " : "", res ? exception2str[res].str : "");
	return res;
}

exception_t subscribe(event_type_t channel, msq_subscription_callback_t callback)
{
	log_verbose(2, "Redis: Enter (%s)\n", __PRETTY_FUNCTION__);
	exception_t res = NO_EXCEPTION;
	if (msq_event_channel_map[channel].channel || msq_event_channel_map[channel].callback) {
		
	} else {
		res = GENERAL_EXCEPTION;
	}
	log_verbose(2, "Redis: Exit %s%s\n", res ? ", Exception Occured: " : "", res ? exception2str[res].str : "");
	return res;
}

exception_t unsubscribe(event_type_t channel)
{
	log_verbose(2, "Redis: Enter (%s)\n", __PRETTY_FUNCTION__);
	exception_t res = NO_EXCEPTION;
	if (!msq_event_channel_map[channel].channel || !msq_event_channel_map[channel].callback) {
		if (msq_event_channel_map[channel].pattern) {
			LOG_NOTICE_DEBUG("Redis: UNSUBSCRIBE channel: '%s:%s'\n", msq_event_channel_map[channel].channel, msq_event_channel_map[channel].pattern);
			redisAsyncCommand(msq_connection_map[PUBLISH].Conn, NULL, NULL, "UNSUBSCRIBE %s:%s", msq_event_channel_map[channel].channel, msq_event_channel_map[channel].pattern);
		} else {
			LOG_NOTICE_DEBUG("Redis: UNSUBSCRIBE channel: '%s'\n", msq_event_channel_map[channel].channel);
			redisAsyncCommand(msq_connection_map[PUBLISH].Conn, NULL, NULL, "UNSUBSCRIBE %s", msq_event_channel_map[channel].channel);
		}
		if (!processRedisAsyncError(msq_connection_map[SUBSCRIBE].Conn)) {
			if (msq_event_channel_map[channel].callback) {
				free(msq_event_channel_map[channel].channel);
			}
			if (msq_event_channel_map[channel].pattern) {
				free(msq_event_channel_map[channel].pattern);
			}
			msq_event_channel_map[channel].callback = NULL;
		} else {
			res = REDIS_EXCEPTION;
		}
	} else {
		res = GENERAL_EXCEPTION;
	}
	log_verbose(2, "Redis: Exit %s%s\n", res ? ", Exception Occured: " : "", res ? exception2str[res].str : "");
	return res;
}
