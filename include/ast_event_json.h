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

#ifndef HAVE_PBX_STASIS_H
int redis_encode_event2msg(char *msg, const size_t msg_len, const struct ast_event *event);
int redis_decode_msg2event(struct ast_event **eventref, enum ast_event_type event_type, const char *msg);
#endif /* HAVE_PBX_STASIS_H */

#endif /* _AST_EVENT_JSON_HEADER_GUARD_H_ */
