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
static exception_t msq_processRedisAsyncConnError(redisAsyncContext *Conn);
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

enum connection_type {
	SOCKET,
	URL
};

pthread_rwlock_t msq_server_rwlock = PTHREAD_RWLOCK_INITIALIZER;
typedef struct msq_servers server_t;
struct msq_servers {
	char *url;
	int port;
	char *socket;
	enum connection_type connection_type;
	redisAsyncContext *redisConn[2];
	//ev_loop *evloop;
	server_t *next;
};
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
exception_t msq_add_server(const char *url, int port, const char *socket)
{
	log_verbose(2, "RedisMSQ: Enter (%s) \n", __PRETTY_FUNCTION__);
	exception_t res = GENERAL_EXCEPTION;
	raii_wrlock(&msq_server_rwlock);
	server_t *server = servers_root;
	server_t *prevserver = server;
	
	// check exists
	while (server) {
		if ( 	(socket && server->socket && !strcasecmp(server->socket, socket)) ||
			((port && server->port && server->port == port) &&
			(url && server->url && !strcasecmp(server->url, url)))
		){
			log_debug("RedisMSQ: Server Already Added: url:'%s', port:%d, socket:'%s'\n", url, port, socket);
			return EXISTS_EXCEPTION;
		}
		
		prevserver = server;
		server = server->next;
	}
		
	if (!server) {
		if (!(server = calloc(1, sizeof(server_t)))) {
			log_debug("RedisMSQ: Malloc Exception\n");
			res = MALLOC_EXCEPTION;
		}
		if (port && url && strlen(url) > 0) {
			log_debug("RedisMSQ: Adding URL Based Connection to Server: [%s:%d]\n", url, port);
			server->url = strdup(url);
			server->port = port;
			server->connection_type = URL;
			res = NO_EXCEPTION;
		} else if (socket && strlen(socket) > 0) {
			log_debug("RedisMSQ: Adding Socket: [%s] Connection\n", socket);
			server->socket = strdup(socket);
			server->connection_type = SOCKET;
			res = NO_EXCEPTION;
		} else {
			log_debug("RedisMSQ: Failed to Add Server [url:%s / port:%d / socket:%s]\n", url, port, socket);
			free(server);
		}
		// tie the node into the list
		if (server) {
			if (prevserver) {
				prevserver->next = server;
			} else {
				servers_root = server;
			}
		}
	}
	log_verbose(2, "RedisMSQ: Function Exit (%s) %s%s%s\n", __PRETTY_FUNCTION__, res ? " [Exception Occured: " : "", res ? exception2str[res].str : "", res ? "]" : "");
	return res;
}

exception_t msq_list_servers()
{
	log_verbose(2, "RedisMSQ: Enter (%s)\n", __PRETTY_FUNCTION__);
	exception_t res = GENERAL_EXCEPTION;
	raii_rdlock(&msq_server_rwlock);
	server_t *server = servers_root;
	while (server) {
		if (server->connection_type == SOCKET) {
			log_debug("RedisMSQ: Socket Based [%s] Connection\n", server->socket);
		} else {
			log_debug("RedisMSQ: Url Based [%s:%d] Connection\n", server->url, server->port);
		}
		server = server->next;
		res = NO_EXCEPTION;
	}
	log_verbose(2, "RedisMSQ: Function Exit (%s) %s%s%s\n", __PRETTY_FUNCTION__, res ? " [Exception Occured: " : "", res ? exception2str[res].str : "", res ? "]" : "");
	return res;
}	

exception_t msq_remove_all_servers()
{
	log_verbose(2, "RedisMSQ: Enter (%s)\n", __PRETTY_FUNCTION__);
	exception_t res = GENERAL_EXCEPTION;
	raii_wrlock(&msq_server_rwlock);
	server_t *server = servers_root;
	server_t *nextserver = NULL;
	
	// check exists
	while (server) {
		nextserver = server->next;
		if (server->socket) {
			free(server->socket);
		}
		if (server->url) {
			free(server->url);
		}
		free(server);
		res = NO_EXCEPTION;
		server = nextserver;
	}
	log_verbose(2, "RedisMSQ: Function Exit (%s) %s%s%s\n", __PRETTY_FUNCTION__, res ? " [Exception Occured: " : "", res ? exception2str[res].str : "", res ? "]" : "");
	return res;
}

exception_t msq_remove_server(const char *url, int port, const char *socket)
{
	log_verbose(2, "RedisMSQ: Enter (%s)\n", __PRETTY_FUNCTION__);
	exception_t res = GENERAL_EXCEPTION;
	raii_wrlock(&msq_server_rwlock);
	server_t *server = servers_root;
	server_t *prevserver = NULL;
	
	// check exists
	while (server) {
		if ( 	(socket && server->socket && !strcasecmp(server->socket, socket)) ||
			((port && server->port && server->port == port) &&
			(url && server->url && !strcasecmp(server->url, url)))
		){
			prevserver->next = server->next;
			if (server->socket) {
				free(server->socket);
			}
			if (server->url) {
				free(server->url);
			}
			free(server);
			server = prevserver->next;
			res = NO_EXCEPTION;
		}
		prevserver = server;
		server = server->next;
	}
	log_verbose(2, "RedisMSQ: Function Exit (%s) %s%s%s\n", __PRETTY_FUNCTION__, res ? " [Exception Occured: " : "", res ? exception2str[res].str : "", res ? "]" : "");
	return res;
}

static exception_t msq_processRedisAsyncConnError(redisAsyncContext *Conn)
{
	log_verbose(2, "RedisMSQ: Enter (%s)\n", __PRETTY_FUNCTION__);
	if (Conn && Conn->err) {
		log_debug("RedisMSQ: Asynchronous Error: '%s'\n", Conn->errstr);
		return REDIS_EXCEPTION;
	}
	return NO_EXCEPTION;
}

exception_t msq_connect_to_next_server()
{
	log_verbose(2, "RedisMSQ: Enter (%s)\n", __PRETTY_FUNCTION__);
	exception_t res = GENERAL_EXCEPTION;
	// check if connected
	msq_disconnect();
		
	raii_wrlock(&msq_server_rwlock);
	server_t *server = current_server;
	if (server && server->next) {
		current_server = current_server->next;
	} else {
		current_server = servers_root;
	}
	
	switch (server->connection_type) {
		case SOCKET:
			server->redisConn[PUBLISH] = redisAsyncConnectUnix(server->socket);
			res |= msq_processRedisAsyncConnError(server->redisConn[PUBLISH]);
			server->redisConn[SUBSCRIBE] = redisAsyncConnectUnix(server->socket);
			res |= msq_processRedisAsyncConnError(server->redisConn[SUBSCRIBE]);
			break;
		case URL:
			server->redisConn[PUBLISH] = redisAsyncConnect(server->url, server->port);
			res |= msq_processRedisAsyncConnError(server->redisConn[PUBLISH]);
			server->redisConn[SUBSCRIBE] = redisAsyncConnect(server->url, server->port);
			res |= msq_processRedisAsyncConnError(server->redisConn[SUBSCRIBE]);
			break;
	}
	
	log_verbose(2, "RedisMSQ: Function Exit (%s) %s%s%s\n", __PRETTY_FUNCTION__, res ? " [Exception Occured: " : "", res ? exception2str[res].str : "", res ? "]" : "");
	return res;
}

exception_t msq_disconnect()
{
	log_verbose(2, "RedisMSQ: Enter (%s)\n", __PRETTY_FUNCTION__);
	exception_t res = GENERAL_EXCEPTION;
	
	if (current_server) {
		raii_wrlock(&msq_server_rwlock);
		if (current_server->redisConn[PUBLISH]) {
			// call general unsubscribe
			redisAsyncDisconnect(current_server->redisConn[PUBLISH]);
			current_server->redisConn[PUBLISH] = NULL;
		}
		if (current_server->redisConn[SUBSCRIBE]) {
			// call general unsubscribe
			redisAsyncDisconnect(current_server->redisConn[SUBSCRIBE]);
			current_server->redisConn[SUBSCRIBE] = NULL;
		}
		res = NO_EXCEPTION;
	}
	log_verbose(2, "RedisMSQ: Function Exit (%s) %s%s%s\n", __PRETTY_FUNCTION__, res ? " [Exception Occured: " : "", res ? exception2str[res].str : "", res ? "]" : "");
	return res;
}

exception_t msq_publish(event_type_t channel, char *publishmsg)
{
	log_verbose(2, "RedisMSQ: Enter (%s)\n", __PRETTY_FUNCTION__);
	exception_t res = NO_EXCEPTION;
	raii_rdlock(&msq_event_channel_map_rwlock);
	if (msq_event_channel_map[channel].channel) {
		log_verbose(1,"RedisMSQ: PUBLISH channel: '%s', mesg: '%s'\n", msq_event_channel_map[channel].channel, publishmsg);
		redisAsyncCommand(current_server->redisConn[PUBLISH], NULL, NULL, "PUBLISH %s %b", msq_event_channel_map[channel].channel, publishmsg, (size_t)strlen(publishmsg));
		res |= msq_processRedisAsyncConnError(current_server->redisConn[PUBLISH]);
	} 
	log_verbose(2, "RedisMSQ: Function Exit (%s) %s%s%s\n", __PRETTY_FUNCTION__, res ? " [Exception Occured: " : "", res ? exception2str[res].str : "", res ? "]" : "");
	return res;
}

exception_t msq_subscribe(event_type_t channel, msq_subscription_callback_t callback)
{
	log_verbose(2, "RedisMSQ: Enter (%s)\n", __PRETTY_FUNCTION__);
	exception_t res = NO_EXCEPTION;
	if (msq_event_channel_map[channel].channel || msq_event_channel_map[channel].callback) {
		if (msq_event_channel_map[channel].pattern) {
			log_verbose(1,"RedisMSQ: SUBSCRIBE channel: '%s:%s'\n", msq_event_channel_map[channel].channel, msq_event_channel_map[channel].pattern);
			redisAsyncCommand(current_server->redisConn[SUBSCRIBE], NULL, NULL, "SUBSCRIBE %s:%s", msq_event_channel_map[channel].channel, msq_event_channel_map[channel].pattern);
		} else {
			log_verbose(1,"RedisMSQ: SUBSCRIBE channel: '%s'\n", msq_event_channel_map[channel].channel);
			redisAsyncCommand(current_server->redisConn[SUBSCRIBE], NULL, NULL, "SUBSCRIBE %s", msq_event_channel_map[channel].channel);
		}
		if (!msq_processRedisAsyncConnError(current_server->redisConn[SUBSCRIBE])) {
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
	log_verbose(2, "RedisMSQ: Function Exit (%s) %s%s%s\n", __PRETTY_FUNCTION__, res ? " [Exception Occured: " : "", res ? exception2str[res].str : "", res ? "]" : "");
	return res;
}

exception_t msq_unsubscribe(event_type_t channel)
{
	log_verbose(2, "RedisMSQ: Enter (%s)\n", __PRETTY_FUNCTION__);
	exception_t res = NO_EXCEPTION;
	if (!msq_event_channel_map[channel].channel || !msq_event_channel_map[channel].callback) {
		if (msq_event_channel_map[channel].pattern) {
			log_verbose(1,"RedisMSQ: UNSUBSCRIBE channel: '%s:%s'\n", msq_event_channel_map[channel].channel, msq_event_channel_map[channel].pattern);
			redisAsyncCommand(current_server->redisConn[SUBSCRIBE], NULL, NULL, "UNSUBSCRIBE %s:%s", msq_event_channel_map[channel].channel, msq_event_channel_map[channel].pattern);
		} else {
			log_verbose(1,"RedisMSQ: UNSUBSCRIBE channel: '%s'\n", msq_event_channel_map[channel].channel);
			redisAsyncCommand(current_server->redisConn[SUBSCRIBE], NULL, NULL, "UNSUBSCRIBE %s", msq_event_channel_map[channel].channel);
		}
		if (!msq_processRedisAsyncConnError(current_server->redisConn[SUBSCRIBE])) {
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
	log_verbose(2, "RedisMSQ: Function Exit (%s) %s%s%s\n", __PRETTY_FUNCTION__, res ? " [Exception Occured: " : "", res ? exception2str[res].str : "", res ? "]" : "");
	return res;
}


/* 
 * eventloop
 */
exception_t msq_start_eventloop() 
{
	return GENERAL_EXCEPTION;
}

exception_t msq_stop_eventloop() 
{
	return GENERAL_EXCEPTION;
}

/*
https://gist.github.com/dspezia/4149768
http://stackoverflow.com/questions/13568465/reconnecting-with-hiredis


pthread_mutex_t msq_eventloop_mutex = PTHREAD_MUTEX_INITIALIZER;
int stopped;

void connectCallBack(const redisAsyncContext *c, int status) {
	if (status != REDIS_OK) {
		log_verbose(2, "Error: %s\n", c->errstr);
		msq_connect_to_next_server();
	} else {
		log_verbose(2, "Connected...\n");
	}
}

void disconnectCallBack(const redisAsyncContext *c, int status) {
	if (status != REDIS_OK) {
		log_verbose(2, "Error: %s\n", c->errstr);
	} else {
		log_verbose(2, "Disonnected...\n");
	}
	pthread_mutex_lock(&msq_eventloop_mutex);
	if (!stopped) {
		pthread_mutex_unlock(&msq_eventloop_mutex);
		msq_connect_to_next_server();
		return;
	}
	pthread_mutex_unlock(&msq_eventloop_mutex);
}

void checkConnections()
{
	if (current->server.ev_loop == NULL) {
		printf("Connecting %d...");
		if (!msq_connect_to_next_server()) {
			redisAeAttach(current->server.ev_loop, singleton.servers[i] );
			redisAsyncSetConnectCallback( singleton.servers[i],connectCallback);
			redisAsyncSetDisconnectCallback( singleton.servers[i],disconnectCallback);
		}
	}
}

int reconnectIfNeeded( struct aeEventLoop *loop, long long id, void *clientData) {
	checkConnections();
	return 1000;
} 
 
int start_main_event_loop(struct aeEventLoop *loop, long, long id, void *ClientData) {
	time_t t = time(NULL);
	
	checkConnections();
	aeCreateTimeEvent(evloop, 5, reconnectIfNeeded, NULL, NULL);
	aeMain(evloop);
}

*/