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
#include <asterisk/stasis.h>

#include "../include/message_serializer.h"
#include "../include/shared.h"


int message2json(char *msg, const size_t msg_len, const struct ast_event *event) 
{
}

int json2message(struct ast_event **eventref, enum ast_event_type event_type, const char *msg)
{
}

#endif