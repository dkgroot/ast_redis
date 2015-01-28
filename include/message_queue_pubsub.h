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
#ifndef _MESSAGE_QUEUE_PUBSUB_H_
#define _MESSAGE_QUEUE_PUBSUB_H_

//#include <stdlib.h>
#include "../include/shared.h"

/* 
 * declarations
 */
typedef enum {
	PUBLISH,
	SUBSCRIBE,
} msq_type_t;

typedef void (*msq_subscription_callback_t)(event_type_t msq_event, void *reply, void *privdata);
typedef void (*msq_connection_callback_t)(int status);
typedef void (*msq_command_callback_t)(void *reply, void *privdata);

typedef struct msq_event_map msq_event_t;

/*
 * public
 */
exception_t msq_add_server(const char *url, int port, const char *socket);
void msq_list_servers();
exception_t msq_remove_all_servers();

exception_t msq_start();
exception_t msq_stop();
event_type_t msq_find_channel(const char *channelname);
exception_t msq_set_channel(event_type_t channel, msq_type_t type, boolean_t onoff);
exception_t msq_publish(event_type_t channel, const char *publishmsg);
exception_t msq_add_subscription(event_type_t channel, const char *channelstr, const char *patternstr, msq_subscription_callback_t callback);
void msq_list_subscriptions();
exception_t msq_drop_all_subscriptions();
exception_t msq_send_subscribe(event_type_t channel);
exception_t msq_send_unsubscribe(event_type_t channel);

exception_t msq_start_eventloop();
exception_t msq_stop_eventloop();
//send_command(...)

#endif /* _MESSAGE_QUEUE_PUBSUB_H_ */
