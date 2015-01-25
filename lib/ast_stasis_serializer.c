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

//typedef struct pbx_event_map {
//	int ast_event_type;
//	const char *name;
//	struct stasis_subscription *handle;
//	pbx_subscription_callback *callback;
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
int pbx_subscribe(pbx_event_type_t event, pbx_subscription_callback *callback)
{
	return -1;
}

int pbx_unsubscribe(pbx_event_type_t event)
{
	return -1;
}

int pbx_publish(pbx_event_type_t event, char *jsonmsgbuffer, size_t buf_len)
{
	return -1;
}

/*
 * private
 */
int message2json(char *msg, const size_t msg_len, const struct ast_event *event) 
{
	return -1;
}

int json2message(struct ast_event **eventref, enum ast_event_type event_type, const char *msg)
{
	return -1;
}

#endif