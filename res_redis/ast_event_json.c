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
#include <asterisk.h>

#define AST_MODULE "res_redis"

ASTERISK_FILE_VERSION(__FILE__, "$Revision: 419592 $")
#include "asterisk/module.h"
#include "asterisk/devicestate.h"
#include "asterisk/event.h"
#include "ast_event_json.h"

/* Fix: required because of broken _ast_event_str_to_ie_type implementation */
static const struct ie_map {
        enum ast_event_ie_pltype ie_pltype;
        const char *name;
} ie_maps[AST_EVENT_IE_TOTAL] = {
        [AST_EVENT_IE_NEWMSGS]             = { AST_EVENT_IE_PLTYPE_UINT, "NewMessages" },
        [AST_EVENT_IE_OLDMSGS]             = { AST_EVENT_IE_PLTYPE_UINT, "OldMessages" },
        [AST_EVENT_IE_MAILBOX]             = { AST_EVENT_IE_PLTYPE_STR,  "Mailbox" },
        [AST_EVENT_IE_UNIQUEID]            = { AST_EVENT_IE_PLTYPE_UINT, "UniqueID" },     
        [AST_EVENT_IE_EVENTTYPE]           = { AST_EVENT_IE_PLTYPE_UINT, "EventType" },
        [AST_EVENT_IE_EXISTS]              = { AST_EVENT_IE_PLTYPE_UINT, "Exists" },
        [AST_EVENT_IE_DEVICE]              = { AST_EVENT_IE_PLTYPE_STR,  "Device" },
        [AST_EVENT_IE_STATE]               = { AST_EVENT_IE_PLTYPE_UINT, "State" },  
        [AST_EVENT_IE_CONTEXT]             = { AST_EVENT_IE_PLTYPE_STR,  "Context" },
        [AST_EVENT_IE_EID]                 = { AST_EVENT_IE_PLTYPE_RAW,  "EntityID" }, 
	//..
};

int fixed_ast_event_str_to_ie_type(const char *str, enum ast_event_ie_type *ie_type)
{
        int i;
        //for (i = 0; i < ARRAY_LEN(ie_maps); i++) {		// broken
        for (i = 1; i < ARRAY_LEN(ie_maps); i++) {
		if (!ie_maps[i].name) {
			continue;
		}
                if (strcasecmp(ie_maps[i].name, str)) {
                        continue;
                }
                *ie_type = i;
                return 0;
        }

        return -1;
}
/* End Fix */


inline static void trim_char_bothends(char *inout, char chr) 
{
	if (!chr || inout[0] == chr) {
		memmove(inout+0, inout+1, strlen(inout)); 	// strip first
	}
	if (!chr || inout[strlen(inout)-1] == chr) {
		inout[strlen(inout)-1] = '\0'; 	 	 	// strip last
	}
}

/* generic ast_event to json encode */
int redis_encode_event2msg(char *msg, const size_t msg_len, const struct ast_event *event) 
{
	unsigned int curpos = 1;
	memset(msg, 0, msg_len);
	msg[0] = '{';
	
	struct ast_event_iterator i;
	if (ast_event_iterator_init(&i, event)) {
		ast_log(LOG_ERROR, "Failed to initialize event iterator.  :-(\n");
		return 0;
	}
	ast_debug(1, "Encoding Event: %s\n", ast_event_get_type_name(event));
	do {
		enum ast_event_ie_type ie_type;
		enum ast_event_ie_pltype ie_pltype;
		const char *ie_type_name;
		ie_type = ast_event_iterator_get_ie_type(&i);
		ie_type_name = ast_event_get_ie_type_name(ie_type);
		ie_pltype = ast_event_get_ie_pltype(ie_type);

		ast_debug(1, "iteration: %d, %s, %d\n", ie_type, ie_type_name, ie_pltype);
		switch (ie_pltype) {
			case AST_EVENT_IE_PLTYPE_UNKNOWN:
			case AST_EVENT_IE_PLTYPE_EXISTS:
				snprintf(msg + curpos, msg_len - curpos, "\"%s\":\"exists\",", ie_type_name);
				break;
			case AST_EVENT_IE_PLTYPE_STR:
				snprintf(msg + curpos, msg_len - curpos, "\"%s\":\"%s\",", ie_type_name, ast_event_iterator_get_ie_str(&i));
				break;
			case AST_EVENT_IE_PLTYPE_UINT:
				snprintf(msg + curpos, msg_len - curpos, "\"%s\":%u,", ie_type_name, ast_event_iterator_get_ie_uint(&i));
				curpos = strlen(msg);
				if (ie_type == AST_EVENT_IE_STATE) {
					snprintf(msg + curpos, msg_len - curpos, "\"statestr\":\"%s\",", ast_devstate_str(ast_event_iterator_get_ie_uint(&i)));
				}
				break;
			case AST_EVENT_IE_PLTYPE_BITFLAGS:
				snprintf(msg + curpos, msg_len - curpos, "\"%s\":%u,", ie_type_name, ast_event_iterator_get_ie_bitflags(&i));
				break;
			case AST_EVENT_IE_PLTYPE_RAW:
				if (ie_type == AST_EVENT_IE_EID) {
					char eid_buf[32];
					ast_eid_to_str(eid_buf, sizeof(eid_buf), ast_event_iterator_get_ie_raw(&i));
					snprintf(msg + curpos, msg_len - curpos, "\"%s\":\"%s\",", ast_event_get_ie_type_name(ie_type), eid_buf);
				} else {
					const void *rawbuf = ast_event_get_ie_raw(event, ie_type);
					snprintf(msg + curpos, msg_len - curpos, "\"%s\",", (unsigned char *)rawbuf);
				}
				break;
		}
		ast_debug(1, "encoded string: '%s'\n", msg);
		curpos = strlen(msg);
	} while (!ast_event_iterator_next(&i));
	
	// replace the last comma with '}' instead
	msg[curpos-1] = '}';
	
	ast_debug(1, "encoded string: '%s'\n", msg);
	return 1;
}

/* generic json to ast_event decoder */
int redis_decode_msg2event(struct ast_event **eventref, const char *msg)
{
	int res = DECODING_ERROR;
	struct ast_event *event = *eventref;
	struct ast_eid eid;
	char *tokenstr = strdupa(msg);
	trim_char_bothends(tokenstr,0);

	if (!(event = ast_event_new(AST_EVENT_DEVICE_STATE_CHANGE, AST_EVENT_IE_END))) {
		return DECODING_ERROR;
	}
	
	char *entry = NULL;
	char *key = NULL;
	char *value = NULL;
	char delims[]=",";
	int cachable = 0;
	
	ast_debug(1, "Decoding Msg2Event: %s\n", tokenstr);
	entry = strtok(tokenstr, delims);
	while (entry) {
		value = strdupa(entry);
		key = strsep(&value, ":");
 		trim_char_bothends(value, '"');
 		trim_char_bothends(key, '"');
		ast_debug(1, "Key: %s, Value: %s\n", key, value);

		enum ast_event_ie_type ie_type;
		enum ast_event_ie_pltype ie_pltype;
		const char *ie_type_name;
		if (!fixed_ast_event_str_to_ie_type(key, &ie_type)){
			ie_pltype = ast_event_get_ie_pltype(ie_type);
			ie_type_name = ast_event_get_ie_type_name(ie_type);
			
			ast_debug(1, "Dealing with %s\n", ie_type_name);

			switch(ie_pltype) {
				case AST_EVENT_IE_PLTYPE_UNKNOWN:
					break;
				case AST_EVENT_IE_PLTYPE_EXISTS:
					ast_event_append_ie_uint(&event, AST_EVENT_IE_EXISTS, atoi(value));
					break;
				case AST_EVENT_IE_PLTYPE_UINT:
					if (ie_type == AST_EVENT_IE_CACHABLE) {
						cachable = atoi(value);
					}
					ast_event_append_ie_uint(&event, ie_type, atoi(value));
					break;
				case AST_EVENT_IE_PLTYPE_BITFLAGS:
					ast_event_append_ie_bitflags(&event, ie_type, atoi(value));
					break;
				case AST_EVENT_IE_PLTYPE_STR:
					ast_event_append_ie_str(&event, ie_type, value);
					break;
				case AST_EVENT_IE_PLTYPE_RAW:
					if (ie_type == AST_EVENT_IE_EID) {
						ast_str_to_eid(&eid, value);
						if (!ast_eid_cmp(&ast_eid_default, &eid)) {
							// Don't feed events back in that originated locally. Quit now.
							res = EID_SELF;
							goto failed;
						}
					        ast_event_append_ie_raw(&event, AST_EVENT_IE_PLTYPE_RAW, &eid, sizeof(eid));
					} else {
						ast_event_append_ie_raw(&event, ie_type, value, strlen(value));
					}
					break;
			}
		}
		entry = strtok(NULL, delims);
	}
	*eventref = event;

	ast_debug(1, "decoded msg into event\n");
	return OK + cachable;

failed:
	ast_event_destroy(event);
	return res;
}
