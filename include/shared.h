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
#define AST_LOG_NOTICE_DEBUG(...) {ast_log(LOG_NOTICE, __VA_ARGS__);ast_debug(1, __VA_ARGS__);}

enum returnvalues {
	MALLOC_ERROR,
	DECODING_ERROR,
	EID_SELF,
	OK,
	OK_CACHABLE,
};

