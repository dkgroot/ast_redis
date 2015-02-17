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

#define ARRAY_LEN(a) (size_t) (sizeof(a) / sizeof(0[a]))
typedef enum { FALSE = 0, TRUE = 1 } boolean_t; 

typedef enum exceptions {
	NO_EXCEPTION		= 0,
	EID_SELF_EXCEPTION	= 1,
	MALLOC_EXCEPTION 	= 100,
	LIBEVENT_EXCEPTION	= 101,
	EXISTS_EXCEPTION	= 102,	
	DECODING_EXCEPTION	= 103,
	REDIS_EXCEPTION 	= 104,
	GENERAL_EXCEPTION	= 105,
} exception_t;

static struct {
	const char *str;
} exception2str[] = {
	[NO_EXCEPTION] = {""},
	[EID_SELF_EXCEPTION] = {"EID Same as our own"},
	[MALLOC_EXCEPTION] = {"Malloc/Free Exception"},
	[LIBEVENT_EXCEPTION] = {"LibEvent Exception"},
	[EXISTS_EXCEPTION] = {"Already Exists Exception"},
	[DECODING_EXCEPTION] = {"Decoding Exception"},
	[REDIS_EXCEPTION] = {"Redis Exception"},
	[GENERAL_EXCEPTION] = {"General Exception"},
};

/*
enum returnvalues {
	ERROR		= 0,
	EID_SELF	= 1,
	OK		= 2,
	OK_CACHABLE	= 3,
};
*/

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
