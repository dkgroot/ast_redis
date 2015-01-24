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
enum returnvalues {
	DECODING_ERROR,
	EID_SELF,
	OK,
	OK_CACHABLE,
};

int redis_encode_event2msg(char *msg, const size_t msg_len, const struct ast_event *event);
int redis_decode_msg2event(struct ast_event **eventref, const char *msg);
