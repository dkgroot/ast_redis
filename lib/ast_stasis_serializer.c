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

#ifdef HAVE_PBX_STASIS_H
#include <asterisk.h>

#define AST_MODULE "res_redis"

ASTERISK_FILE_VERSION(__FILE__, "$Revision: 419592 $")
#include <asterisk/module.h>
#include <asterisk/devicestate.h>
#include <asterisk/event.h>
#include <asterisk/event_defs.h>
#include <asterisk/stasis.h>

#include "../include/message_serializer.h"
#include "../include/shared.h"

/*
 * declaration
 */
static void ast_event_cb(const struct ast_event *event, void *data);

/*
 * globals
 */
AST_RWLOCK_DEFINE_STATIC(event_map_lock);
 

//typedef struct pbx_event_map {
//	int ast_event_type;
//	const char *name;
//	struct stasis_subscription *sub;
//	pbx_subscription_callback_t *callback;
//} pbx_event_map_t;
static pbx_event_map_t event_map[AST_EVENT_TOTAL] = {
	[PBX_EVENT_MWI] =                 {.ast_event_type = AST_EVENT_MWI, .name = "mwi"},
	[PBX_EVENT_DEVICE_STATE_CHANGE] = {.ast_event_type = AST_EVENT_DEVICE_STATE_CHANGE, .name = "device_state_change"},
	[PBX_EVENT_DEVICE_STATE] =        {.ast_event_type = AST_EVENT_DEVICE_STATE, .name = "device_state"},
	[PBX_EVENT_PING] =                {.ast_event_type = AST_EVENT_PING, .name = "ping"},
};

/*
 * public 
 */
int pbx_subscribe(pbx_event_type_t event_type, pbx_subscription_callback *callback)
{
/*	ast_rwlock_rdlock(&event_map_lock);
	if (event_map[event_type].sub) {
		pbx_unsubscribe(event_type);
	} else {
		event_map[event_type].callback = callback;
		event_map[event_type].sub = ast_event_subscribe_new(event_map[event_type].ast_event_type, ast_event_cb, &event_type);
	}
	ast_rwlock_unlock(&event_map_lock);
	return 0;*/
	return -1;
}

int pbx_unsubscribe(pbx_event_type_t event_type)
{
/*	ast_rwlock_rdlock(&event_map_lock);
	if (!event_map[event_type].sub) {
		// not subscribed error
	} else {
		event_map[event_type].sub = ast_event_unsubscribe((struct ast_event_sub *)event_map[event_type].sub);
		event_map[event_type].callback = NULL;
	}
	ast_rwlock_unlock(&event_map_lock);
	return 0;*/
	return -1;
}

int pbx_publish(pbx_event_type_t event_type, char *jsonmsgbuffer, size_t buf_len)
{
	return -1;
}

/*
 * private
 */
static void ast_event_cb(void *userdata, struct stasis_subscription *sub, struct stasis_message *smsg);
	pbx_event_type_t event_type = (pbx_event_type_t)data;
/*	char *jsonbuffer = NULL;
	if (!(jsonbuffer = malloc(MAX_JSON_BUFFERLEN))) {
		// malloc error
	}
	if (message2json(jsonbuffer, MAX_JSON_BUFFERLEN, event)) {
		event_map[event_type].callback(event_type, jsonbuffer);
	} else {
		// error
	}
	ast_free(jsonbuffer);*/
}

#endif