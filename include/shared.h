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

/*!
 * \file
 * \author Diederik de Groot <ddegroot@users.sf.net>
 *
 * This module is based on the res_corosync module.
 */
#ifndef _SHARED_GUARD_H_
#define _SHARED_GUARD_H_

typedef enum exceptions {
	NO_EXCEPTION,
	MALLOC_EXCEPTION,
	EXISTS_EXCEPTION,
	DECODING_EXCEPTION,
	REDIS_EXCEPTION,
	GENERAL_EXCEPTION,
} exception_t;

static struct {
	const char *str;
} exception2str[] = {
	[NO_EXCEPTION] = {""},
	[MALLOC_EXCEPTION] = {"Malloc/Free Exception"},
	[EXISTS_EXCEPTION] = {"Already Exists Exception"},
	[DECODING_EXCEPTION] = {"Decoding Exception"},
	[REDIS_EXCEPTION] = {"Redis Exception"},
	[GENERAL_EXCEPTION] = {"General Exception"},
};

enum returnvalues {
	MALLOC_ERROR,
	DECODING_ERROR,
	EID_SELF,
	OK,
	OK_CACHABLE,
};

typedef enum event_type {
	EVENT_ALL			= 0x00,
	EVENT_CUSTOM			= 0x01,
	EVENT_MWI			= 0x02,
	EVENT_SUB			= 0x03,
	EVENT_UNSUB			= 0x04,
	EVENT_DEVICE_STATE		= 0x05,
	EVENT_DEVICE_STATE_CHANGE 	= 0x06,
	EVENT_CEL			= 0x07,
	EVENT_SECURITY			= 0x08,
	EVENT_NETWORK_CHANGE		= 0x09,
	EVENT_PRESENCE_STATE		= 0x0a,
	EVENT_ACL_CHANGE		= 0x0b,
	EVENT_PING			= 0x0c,
	EVENT_TOTAL			= 0x0d,
} event_type_t;

/* pipe logging back to asterisk */
void _log_verbose(int level, const char *file, int line, const char *function, const char *fmt, ...) __attribute__((format(printf, 5, 6)));

#define log_debug(...) _log_verbose(4, __FILE__, __LINE__, __PRETTY_FUNCTION__, __VA_ARGS__)
#define log_verbose(_level, ...) _log_verbose(_level, __FILE__, __LINE__, __PRETTY_FUNCTION__, __VA_ARGS__)
/* end logging */

/* */
#endif /* _SHARED_GUARD_H_ */
