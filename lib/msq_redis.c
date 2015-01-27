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

static void redis_ping_subscription_cb(event_type_t msq_event, void *reply, void *privdata);

static exception_t msq_processRedisAsyncConnError(redisAsyncContext *Conn);
exception_t _msq_remove_server(const char *url, int port, const char *socket);
exception_t _msq_connect_to_next_server();
exception_t _msq_disconnect();

exception_t _msq_remove_subscription(event_type_t channel);
exception_t _msq_toggle_subscriptions(boolean_t on);
exception_t _msq_send_subscribe(event_type_t channel);
exception_t _msq_send_unsubscribe(event_type_t channel);

/*
 * global
 */
pthread_rwlock_t msq_event_map_rwlock = PTHREAD_RWLOCK_INITIALIZER;
struct msq_event_map {
	const char *name;
	boolean_t publish;
	boolean_t subscribe;
	boolean_t active;
	char *channel;
	char *pattern;
	msq_subscription_callback_t callback;
};
static msq_event_t msq_event_map[] = {
	[EVENT_MWI]			= {.name="mwi",},
	[EVENT_DEVICE_STATE]		= {.name="device_state",},
	[EVENT_DEVICE_STATE_CHANGE] 	= {.name="device_state_change"},
	[EVENT_PING]			= {.name="ping", .publish=TRUE, .subscribe=TRUE, .active=FALSE, .channel="asterisk:ping", .pattern="", .callback=redis_ping_subscription_cb}
};

enum connection_type {
	NONE,
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
	pthread_t thread;
	struct event_base *evloop_base;
	server_t *next;
};
static server_t *servers_root = NULL;
static server_t *current_server = NULL;

pthread_mutex_t msq_startstop_mutex = PTHREAD_MUTEX_INITIALIZER;
volatile boolean_t stopped;

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
	log_verbose(2, "RedisMSQ: (%s) enter \n", __PRETTY_FUNCTION__);
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
			server = NULL;
		}
		server->next = NULL;
		// tie the node into the list
		if (server) {
			if (servers_root == NULL) {
				servers_root = server;
			} else if (prevserver) {
				prevserver->next = server;
			} else {
				log_debug("RedisMSQ: Failed to Add Server to the list.\n");
				free(server);
				server = NULL; 
				res = GENERAL_EXCEPTION;
			}
		}
	}
	log_verbose(2, "RedisMSQ: (%s) exit %s%s%s\n", __PRETTY_FUNCTION__, res ? " [Exception Occured: " : "", res ? exception2str[res].str : "", res ? "]" : "");
	return res;
}

void msq_list_servers()
{
	raii_rdlock(&msq_server_rwlock);
	server_t *server = servers_root;

	int index = 0;
	log_verbose(1,"+-------+--------+--------------------------------------------------------------+\n");
	log_verbose(1,"| index | type   | connection                                                   |\n");
	log_verbose(1,"+-------|--------|--------------------------------------------------------------+\n");
	while (server) {
		if (server->connection_type == SOCKET) {
			log_verbose(1,"| %5.5d | %6s | %60.60s |\n", index, "socket", server->socket);
		} else {
			char serverurlport [60];
			snprintf(serverurlport, 60, "%s:%d", server->url, server->port);
			log_verbose(1,"| %5.5d | %6s | %60.60s |\n", index, "url", serverurlport);
		}
		index++;
		server = server->next;
	}
	log_verbose(1,"+-------+--------+--------------------------------------------------------------+\n");
}	

exception_t msq_remove_all_servers()
{
	log_verbose(2, "RedisMSQ: (%s) enter\n", __PRETTY_FUNCTION__);
	exception_t res = GENERAL_EXCEPTION;
	raii_wrlock(&msq_server_rwlock);
	server_t *nextserver = servers_root;
	
	while (servers_root) {
		nextserver = servers_root->next;
		if (servers_root->socket) {
			free(servers_root->socket);
			servers_root->socket = NULL;
		}
		if (servers_root->url) {
			free(servers_root->url);
			servers_root->port = 0;
			servers_root->url = NULL;
		}
		servers_root->connection_type = NONE;
		free(servers_root);
		servers_root = nextserver;
		res = NO_EXCEPTION;
	}
	log_verbose(2, "RedisMSQ: (%s) exit %s%s%s\n", __PRETTY_FUNCTION__, res ? " [Exception Occured: " : "", res ? exception2str[res].str : "", res ? "]" : "");
	return res;
}

exception_t _msq_remove_server(const char *url, int port, const char *socket)
{
	log_verbose(2, "RedisMSQ: (%s) enter\n", __PRETTY_FUNCTION__);
	exception_t res = GENERAL_EXCEPTION;
	raii_wrlock(&msq_server_rwlock);
	
	server_t *server = NULL;
	server_t *prevserver = NULL;
	server_t *node2bfreed = NULL;
	
	// check exists
	for (server = servers_root; server != NULL; server = server->next) {
		if ( 	(socket && server->socket && !strcasecmp(server->socket, socket)) ||
			((port && server->port && server->port == port) &&
			(url && server->url && !strcasecmp(server->url, url)))
		){
			node2bfreed = server;
			break;
		}
		prevserver = server;
	}
	
	// found ?
	if (node2bfreed == NULL) {
		log_verbose(1, "server to be removed could not be found\n");
		return res;
	} else if (prevserver == NULL) {
		servers_root = node2bfreed->next;
	} else {
		prevserver = node2bfreed->next;
	}
	
	// found -> free content
	if (server->socket) {
		free(server->socket);
		server->socket = NULL;
	}
	if (server->url) {
		free(server->url);
		server->url = NULL;
	}
	free(server);
	res = NO_EXCEPTION;

	log_verbose(2, "RedisMSQ: (%s) exit %s%s%s\n", __PRETTY_FUNCTION__, res ? " [Exception Occured: " : "", res ? exception2str[res].str : "", res ? "]" : "");
	return res;
}

static exception_t msq_processRedisAsyncConnError(redisAsyncContext *Conn)
{
	log_verbose(2, "RedisMSQ: (%s) enter\n", __PRETTY_FUNCTION__);
	if (Conn && Conn->err) {
		log_debug("RedisMSQ: Asynchronous Error: '%s'\n", Conn->errstr);
		return REDIS_EXCEPTION;
	}
	return NO_EXCEPTION;
}

exception_t _msq_connect_to_next_server()
{
	log_verbose(2, "RedisMSQ: (%s) enter\n", __PRETTY_FUNCTION__);
	exception_t res = NO_EXCEPTION;
	// check if connected
	if (current_server) {
		_msq_disconnect();
	}
		
	raii_wrlock(&msq_server_rwlock);
	server_t *server = current_server;
	if (server && server->next) {
		current_server = current_server->next;
	} else {
		current_server = servers_root;
	}
	
	switch (current_server->connection_type) {
		case SOCKET:
			current_server->redisConn[PUBLISH] = redisAsyncConnectUnix(current_server->socket);
			res |= msq_processRedisAsyncConnError(current_server->redisConn[PUBLISH]);
			current_server->redisConn[SUBSCRIBE] = redisAsyncConnectUnix(current_server->socket);
			res |= msq_processRedisAsyncConnError(current_server->redisConn[SUBSCRIBE]);
			break;
		case URL:
			current_server->redisConn[PUBLISH] = redisAsyncConnect(current_server->url, current_server->port);
			res |= msq_processRedisAsyncConnError(current_server->redisConn[PUBLISH]);
			current_server->redisConn[SUBSCRIBE] = redisAsyncConnect(current_server->url, current_server->port);
			res |= msq_processRedisAsyncConnError(current_server->redisConn[SUBSCRIBE]);
			break;
		default:
			log_verbose(2, "RedisMSQ: Cannot hanfle connection_type\n");
	}
	
	//res |= msq_start_eventloop();
	res |= _msq_toggle_subscriptions(TRUE);
	
	log_verbose(2, "RedisMSQ: (%s) exit %s%s%s\n", __PRETTY_FUNCTION__, res ? " [Exception Occured: " : "", res ? exception2str[res].str : "", res ? "]" : "");
	return res;
}

exception_t _msq_disconnect()
{
	log_verbose(2, "RedisMSQ: (%s) enter\n", __PRETTY_FUNCTION__);
	exception_t res = NO_EXCEPTION;
	
	res |= _msq_toggle_subscriptions(FALSE);
	
	if (!res && current_server) {
		raii_wrlock(&msq_server_rwlock);
		if (current_server->redisConn[PUBLISH]) {
			// call general unsubscribe
			redisAsyncDisconnect(current_server->redisConn[PUBLISH]);
			res |= msq_processRedisAsyncConnError(current_server->redisConn[PUBLISH]);
			current_server->redisConn[PUBLISH] = NULL;
		}
		if (current_server->redisConn[SUBSCRIBE]) {
			// call general unsubscribe
			redisAsyncDisconnect(current_server->redisConn[SUBSCRIBE]);
			res |= msq_processRedisAsyncConnError(current_server->redisConn[SUBSCRIBE]);
			current_server->redisConn[SUBSCRIBE] = NULL;
		}
		//res |= msq_stop_eventloop();
	}
	log_verbose(2, "RedisMSQ: (%s) exit %s%s%s\n", __PRETTY_FUNCTION__, res ? " [Exception Occured: " : "", res ? exception2str[res].str : "", res ? "]" : "");
	return res;
}

event_type_t msq_start()
{
	log_verbose(2, "RedisMSQ: (%s) enter\n", __PRETTY_FUNCTION__);
	exception_t res = NO_EXCEPTION;
	
	pthread_mutex_lock(&msq_startstop_mutex);
	_msq_connect_to_next_server();
	stopped = FALSE;
	pthread_mutex_unlock(&msq_startstop_mutex);
	
	return res;
}

event_type_t msq_stop()
{
	log_verbose(2, "RedisMSQ: (%s) enter\n", __PRETTY_FUNCTION__);
	exception_t res = NO_EXCEPTION;

	pthread_mutex_lock(&msq_startstop_mutex);
	stopped = TRUE;
	_msq_disconnect();
	pthread_mutex_unlock(&msq_startstop_mutex);
	return res;
}

event_type_t msq_find_channel(const char *channelname) 
{
	event_type_t chan;
	raii_rdlock(&msq_event_map_rwlock);
	for (chan = 0; chan < ARRAY_LEN(msq_event_map); chan++ ) {
		if (msq_event_map[chan].name && !strcasecmp(channelname, msq_event_map[chan].name)) {
			return chan;
		}
	}
	return 0;
}

exception_t msq_set_channel(event_type_t channel, msq_type_t type, boolean_t onoff)
{
	log_verbose(2, "RedisMSQ: (%s) enter\n", __PRETTY_FUNCTION__);
	if (msq_event_map[channel].name) {
		if (type == PUBLISH) {
			msq_event_map[channel].publish = onoff;
		} else if (type == SUBSCRIBE) {
			msq_event_map[channel].subscribe = onoff;
		} else {
			log_debug("Error: Unknown msq_type: [%d]", type);
		}
	} else {
		log_debug("Error: Unknown channel: [%d]", channel);
	}
	exception_t res = NO_EXCEPTION;
	log_verbose(2, "RedisMSQ: (%s) exit %s%s%s\n", __PRETTY_FUNCTION__, res ? " [Exception Occured: " : "", res ? exception2str[res].str : "", res ? "]" : "");
	return res;
}

exception_t msq_publish(event_type_t channel, const char *publishmsg)
{
	log_verbose(2, "RedisMSQ: (%s) enter\n", __PRETTY_FUNCTION__);
	exception_t res = NO_EXCEPTION;
	raii_rdlock(&msq_event_map_rwlock);
	if (msq_event_map[channel].channel) {
		log_verbose(1,"RedisMSQ: PUBLISH channel: '%s', mesg: '%s'\n", msq_event_map[channel].channel, publishmsg);
		redisAsyncCommand(current_server->redisConn[PUBLISH], NULL, NULL, "PUBLISH %s %b", msq_event_map[channel].channel, publishmsg, (size_t)strlen(publishmsg));
		res |= msq_processRedisAsyncConnError(current_server->redisConn[PUBLISH]);
	} 
	log_verbose(2, "RedisMSQ: (%s) exit %s%s%s\n", __PRETTY_FUNCTION__, res ? " [Exception Occured: " : "", res ? exception2str[res].str : "", res ? "]" : "");
	return res;
}

exception_t msq_add_subscription(event_type_t channel, const char *channelstr, const char *patternstr, msq_subscription_callback_t callback)
{
	log_verbose(2, "RedisMSQ: (%s) enter\n", __PRETTY_FUNCTION__);
	exception_t res = GENERAL_EXCEPTION;

	raii_wrlock(&msq_event_map_rwlock);
	if (msq_event_map[channel].name) {
		if (msq_event_map[channel].channel || msq_event_map[channel].callback || msq_event_map[channel].pattern) {
			// previous subscription
			log_debug("Error: previous subscription exists");
			return res;
		} else {
			if (!channelstr || !callback) {
				log_debug("Error: add_subscription required both a channelstr and a callback");
				return res;
			}
			if (channelstr) {
				msq_event_map[channel].channel = strdup(channelstr);
			}
			if (callback) {
				msq_event_map[channel].callback = callback;
			}
			if (patternstr) {
				msq_event_map[channel].pattern = strdup(patternstr);
			}
			msq_event_map[channel].active = FALSE;
			res = NO_EXCEPTION;
		}
	} else {
		log_debug("Error: not a valid channel");
		return res;
	}
	log_verbose(2, "RedisMSQ: (%s) exit %s%s%s\n", __PRETTY_FUNCTION__, res ? " [Exception Occured: " : "", res ? exception2str[res].str : "", res ? "]" : "");
	return res;
}

void msq_list_subscriptions() 
{
	event_type_t chan = 0;

	log_verbose(1,"+----------------------+--------------------------------+--------------------------------+-----------+-----------+--------+\n");
	log_verbose(1,"| channelname          | channel                        | pattern                        | publish   | subscribe | active |\n");
	log_verbose(1,"+----------------------|--------------------------------|--------------------------------|-----------|-----------|--------+\n");
	raii_rdlock(&msq_event_map_rwlock);
	for (chan = 0; chan < ARRAY_LEN(msq_event_map); chan++ ) {
		if (msq_event_map[chan].name) {
			log_verbose(1,"| %20.20s | %30.30s | %30.30s | %9s | %9s | %6s |\n", 
				msq_event_map[chan].name, 
				msq_event_map[chan].channel, 
				msq_event_map[chan].pattern, 
				msq_event_map[chan].publish ? "ON" : "OFF", 
				msq_event_map[chan].subscribe ? "ON" : "OFF",
				msq_event_map[chan].active ? "ON" : "OFF"
			);
		}
	}
	log_verbose(1,"+----------------------|--------------------------------|--------------------------------|-----------|-----------|--------+\n");
}

exception_t _msq_drop_all_subscriptions(event_type_t channel)
{
	log_verbose(2, "RedisMSQ: (%s) enter\n", __PRETTY_FUNCTION__);
	exception_t res = GENERAL_EXCEPTION;
	event_type_t chan = 0;

	raii_wrlock(&msq_event_map_rwlock);
	for (chan = 0; chan < ARRAY_LEN(msq_event_map); chan++ ) {
		if (msq_event_map[channel].name) {
			if (msq_event_map[channel].channel) {
				free(msq_event_map[channel].channel);
			}
			if (msq_event_map[channel].pattern) {
				free(msq_event_map[channel].pattern);
			}
			msq_event_map[channel].callback = NULL;
			msq_event_map[channel].active = FALSE;
			res = NO_EXCEPTION;
		}
	}
	log_verbose(2, "RedisMSQ: (%s) exit %s%s%s\n", __PRETTY_FUNCTION__, res ? " [Exception Occured: " : "", res ? exception2str[res].str : "", res ? "]" : "");
	return res;	
}

exception_t _msq_remove_subscription(event_type_t channel)
{
	log_verbose(2, "RedisMSQ: (%s) enter\n", __PRETTY_FUNCTION__);
	exception_t res = GENERAL_EXCEPTION;
	raii_wrlock(&msq_event_map_rwlock);
	if (msq_event_map[channel].name) {
		if (!msq_event_map[channel].channel || !msq_event_map[channel].callback) {
			log_debug("Error: no previous subscription exists");
			return res;
		} else {
			if (msq_event_map[channel].channel) {
				free(msq_event_map[channel].channel);
			}
			if (msq_event_map[channel].pattern) {
				free(msq_event_map[channel].pattern);
			}
			msq_event_map[channel].callback = NULL;
			msq_event_map[channel].active = FALSE;
			res = NO_EXCEPTION;
		}
	} else {
		log_debug("Error: not a valid channel");
		return res;
	}
	log_verbose(2, "RedisMSQ: (%s) exit %s%s%s\n", __PRETTY_FUNCTION__, res ? " [Exception Occured: " : "", res ? exception2str[res].str : "", res ? "]" : "");
	return res;
}

exception_t _msq_toggle_subscriptions(boolean_t on)
{
	log_verbose(2, "RedisMSQ: (%s) enter\n", __PRETTY_FUNCTION__);
	exception_t res = NO_EXCEPTION;
	event_type_t chan = 0;
	raii_wrlock(&msq_event_map_rwlock);
	for (chan = 0; chan < ARRAY_LEN(msq_event_map) && !res; chan++ ) {
		if (msq_event_map[chan].subscribe) {
			if (on) {
				res |= _msq_send_subscribe(chan);
				if (!res) {
					msq_event_map[chan].active = TRUE;
				}
			} else {
				res |= _msq_send_unsubscribe(chan);
				if (!res) {
					msq_event_map[chan].active = FALSE;
				}
			}
		}
	}
	log_verbose(2, "RedisMSQ: (%s) exit %s%s%s\n", __PRETTY_FUNCTION__, res ? " [Exception Occured: " : "", res ? exception2str[res].str : "", res ? "]" : "");
	return res;
}

exception_t _msq_send_subscribe(event_type_t channel)
{
	log_verbose(2, "RedisMSQ: (%s) enter\n", __PRETTY_FUNCTION__);
	exception_t res = NO_EXCEPTION;
	
	raii_rdlock(&msq_event_map_rwlock);
	if (msq_event_map[channel].name) {
		if (msq_event_map[channel].channel || msq_event_map[channel].callback) {
			if (msq_event_map[channel].pattern) {
				log_verbose(1,"RedisMSQ: SUBSCRIBE channel: '%s:%s'\n", msq_event_map[channel].channel, msq_event_map[channel].pattern);
				redisAsyncCommand(current_server->redisConn[SUBSCRIBE], NULL, NULL, "SUBSCRIBE %s:%s", msq_event_map[channel].channel, msq_event_map[channel].pattern);
				res |= msq_processRedisAsyncConnError(current_server->redisConn[SUBSCRIBE]);
			} else {
				log_verbose(1,"RedisMSQ: SUBSCRIBE channel: '%s'\n", msq_event_map[channel].channel);
				redisAsyncCommand(current_server->redisConn[SUBSCRIBE], NULL, NULL, "SUBSCRIBE %s", msq_event_map[channel].channel);
				res |= msq_processRedisAsyncConnError(current_server->redisConn[SUBSCRIBE]);
			}
		} else {
			res = GENERAL_EXCEPTION;
		}
	} else {
		log_debug("Error: not a valid channel");
		return res;
	}
	log_verbose(2, "RedisMSQ: (%s) exit %s%s%s\n", __PRETTY_FUNCTION__, res ? " [Exception Occured: " : "", res ? exception2str[res].str : "", res ? "]" : "");
	return res;
}

exception_t _msq_send_unsubscribe(event_type_t channel)
{
	log_verbose(2, "RedisMSQ: (%s) enter\n", __PRETTY_FUNCTION__);
	exception_t res = NO_EXCEPTION;
	raii_rdlock(&msq_event_map_rwlock);
	if (msq_event_map[channel].name) {
		if (!msq_event_map[channel].channel || !msq_event_map[channel].callback) {
			if (msq_event_map[channel].pattern) {
				log_verbose(1,"RedisMSQ: UNSUBSCRIBE channel: '%s:%s'\n", msq_event_map[channel].channel, msq_event_map[channel].pattern);
				redisAsyncCommand(current_server->redisConn[SUBSCRIBE], NULL, NULL, "UNSUBSCRIBE %s:%s", msq_event_map[channel].channel, msq_event_map[channel].pattern);
				res |= msq_processRedisAsyncConnError(current_server->redisConn[SUBSCRIBE]);
			} else {
				log_verbose(1,"RedisMSQ: UNSUBSCRIBE channel: '%s'\n", msq_event_map[channel].channel);
				redisAsyncCommand(current_server->redisConn[SUBSCRIBE], NULL, NULL, "UNSUBSCRIBE %s", msq_event_map[channel].channel);
				res |= msq_processRedisAsyncConnError(current_server->redisConn[SUBSCRIBE]);
			}
		} else {
			res = GENERAL_EXCEPTION;
		}
	} else {
		log_debug("Error: not a valid channel");
		return res;
	}
	log_verbose(2, "RedisMSQ: (%s) exit %s%s%s\n", __PRETTY_FUNCTION__, res ? " [Exception Occured: " : "", res ? exception2str[res].str : "", res ? "]" : "");
	return res;
}

static void redis_ping_subscription_cb(event_type_t msq_event, void *reply, void *privdata) 
{
	log_verbose(2, "Ping Callback...\n");
}

static void redis_connect_cb(const redisAsyncContext *c, int status) 
{
	if (status != REDIS_OK) {
		log_verbose(2, "Error: %s\n", c->errstr);
		//_msq_connect_to_next_server();
	} else {
		log_verbose(2, "Connected...\n");
	}
}

static void redis_disconnect_cb(const redisAsyncContext *c, int status) 
{
	if (status != REDIS_OK) {
		log_verbose(2, "Error: %s\n", c->errstr);
	} else {
		log_verbose(2, "Disonnected...\n");
	}
	pthread_mutex_lock(&msq_startstop_mutex);
	if (!stopped) {
		pthread_mutex_unlock(&msq_startstop_mutex);
		_msq_connect_to_next_server();
		return;
	}
	pthread_mutex_unlock(&msq_startstop_mutex);
}


/* 
 * eventloop
 */
#define EVTHREAD_USE_PTHREADS_IMPLEMENTED 1
static void *eventloop_dispatch_thread(void *data) 
{
	log_debug("Entering eventlog_dispatch thread");
	struct event_base *eventbase = data;
	redisLibeventAttach(current_server->redisConn[PUBLISH],eventbase);
	redisLibeventAttach(current_server->redisConn[SUBSCRIBE],eventbase);
	
	redisAsyncSetConnectCallback(current_server->redisConn[PUBLISH], redis_connect_cb);
	redisAsyncSetDisconnectCallback(current_server->redisConn[PUBLISH], redis_disconnect_cb);
	
	redisAsyncSetConnectCallback(current_server->redisConn[SUBSCRIBE], redis_connect_cb);
	redisAsyncSetDisconnectCallback(current_server->redisConn[SUBSCRIBE], redis_disconnect_cb);

	event_base_dispatch(eventbase);
	log_debug("Exiting eventlog_dispatch thread");
	
	// cleanup ?
	
	return NULL;
} 

exception_t msq_start_eventloop() 
{
	//raii_wrlock(&msq_server_rwlock);
	exception_t res = GENERAL_EXCEPTION;
	if (!current_server) {
		return res;
	}
	event_init();
	if ((current_server->evloop_base = event_base_new()) == NULL) {
		log_debug("Unable to create socket accept event base");
		return LIBEVENT_EXCEPTION;
	}
	if (pthread_create(&current_server->thread, NULL, eventloop_dispatch_thread, current_server->evloop_base)) {
		log_debug("Error starting Redis dispatch thread.\n");
		return LIBEVENT_EXCEPTION;
	}
	return NO_EXCEPTION;
}

exception_t msq_stop_eventloop() 
{
	//raii_wrlock(&msq_server_rwlock);
	if (event_base_loopexit(current_server->evloop_base, NULL)) {
		log_debug("Error shutting down server");
		return LIBEVENT_EXCEPTION;
	}
	int s = pthread_join(current_server->thread, NULL);
	if (s != 0)
		log_debug("Error thread join.\n");
	event_base_free(current_server->evloop_base);
	return NO_EXCEPTION;
}

/*
https://gist.github.com/dspezia/4149768
http://stackoverflow.com/questions/13568465/reconnecting-with-hiredis




void checkConnections()
{
	if (current->server.ev_loop == NULL) {
		printf("Connecting %d...");
		if (!_msq_connect_to_next_server()) {
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