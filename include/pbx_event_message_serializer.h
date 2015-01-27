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
#ifndef _AST_EVENT_MESSAGE_SERIALIZER_H_
#define _AST_EVENT_MESSAGE_SERIALIZER_H_

#include "shared.h"

#define MAX_JSON_BUFFERLEN 1024

typedef void (*pbx_subscription_callback_t) (event_type_t event_type, char *data);
typedef struct pbx_event_map pbx_event_map_t;

exception_t pbx_subscribe(event_type_t event_type, pbx_subscription_callback_t callback);
exception_t pbx_unsubscribe(event_type_t event_type);
exception_t pbx_publish(event_type_t event_type, char *jsonmsgbuffer, size_t buf_len);

/* should become private instead / to be removed*/
exception_t message2json(char *jsonmsgbuffer, const size_t msg_len, const struct ast_event *event);
exception_t json2message(struct ast_event **eventref, enum ast_event_type event_type, const char *jsonmsgbuffer);

#endif /* _AST_EVENT_MESSAGE_SERIALIZER_H_ */
