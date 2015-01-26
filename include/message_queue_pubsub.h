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

#include <stdlib.h>
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

typedef struct msq_event_channel_map msq_event_channel_t;

/*
 * public
 */
exception_t addserver(char *url, int port, char *socket);
exception_t removeserver(char *url, int port, char *socket);
exception_t connect_to_next_server();
exception_t disconnect();

exception_t publish(event_type_t channel, char *publishmsg);
exception_t subscribe(event_type_t channel, msq_subscription_callback_t callback);
//send_command(...)

#endif /* _MESSAGE_QUEUE_PUBSUB_H_ */
