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
#ifndef _AST_EVENT_JSON_HEADER_GUARD_H_
#define _AST_EVENT_JSON_HEADER_GUARD_H_

#define MAX_JSON_BUFFERLEN 1024

typedef enum pbx_event_type {
	PBX_EVENT_ALL                 = 0x00,
	PBX_EVENT_CUSTOM              = 0x01,
	PBX_EVENT_MWI                 = 0x02,
	PBX_EVENT_SUB                 = 0x03,
	PBX_EVENT_UNSUB               = 0x04,
	PBX_EVENT_DEVICE_STATE        = 0x05,
	PBX_EVENT_DEVICE_STATE_CHANGE = 0x06,
	PBX_EVENT_CEL                 = 0x07,
	PBX_EVENT_SECURITY	      = 0x08,
	PBX_EVENT_NETWORK_CHANGE      = 0x09,
	PBX_EVENT_PRESENCE_STATE      = 0x0a,
	PBX_EVENT_ACL_CHANGE	      = 0x0b,
	PBX_EVENT_PING	              = 0x0c,
	PBX_EVENT_TOTAL	              = 0x0d,	
} pbx_event_type_t;

typedef void (*pbx_subscription_callback_t) (pbx_event_type_t event_type, char *data);

typedef struct pbx_event_map {
	int ast_event_type;
	const char *name;
	//struct stasis_subscription *sub;
	struct ast_event_sub *sub;
	pbx_subscription_callback_t callback;
} pbx_event_map_t;

int pbx_subscribe(pbx_event_type_t event_type, pbx_subscription_callback_t callback);
int pbx_unsubscribe(pbx_event_type_t event_type);
int pbx_publish(pbx_event_type_t event_type, char *jsonmsgbuffer, size_t buf_len);

/* should become private instead / to be removed*/
int message2json(char *jsonmsgbuffer, const size_t msg_len, const struct ast_event *event);
int json2message(struct ast_event **eventref, enum ast_event_type event_type, const char *jsonmsgbuffer);

#endif /* _AST_EVENT_JSON_HEADER_GUARD_H_ */
