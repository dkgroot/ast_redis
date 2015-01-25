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

#include <asterisk.h>

#define AST_MODULE "res_redis"

ASTERISK_FILE_VERSION(__FILE__, "$Revision: 419592 $")
#include <asterisk/module.h>
#include <asterisk/devicestate.h>
#include <asterisk/event.h>

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
////	struct stasis_subscription *sub;
//	struct ast_event_sub *sub;
//	pbx_subscription_callback_t callback;
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
//static void ast_event_cb(void *userdata, struct stasis_subscription *sub, struct stasis_message *smsg);
static void ast_event_cb(const struct ast_event *event, void *data) {
	pbx_event_type_t event_type = (pbx_event_type_t)data;
	char *jsonbuffer = NULL;
	if (!(jsonbuffer = malloc(MAX_JSON_BUFFERLEN))) {
		// malloc error
	}
	if (message2json(jsonbuffer, MAX_JSON_BUFFERLEN, event)) {
		event_map[event_type].callback(event_type, jsonbuffer);
	} else {
		// error
	}
	ast_free(jsonbuffer);
}

/* public */
int pbx_subscribe(pbx_event_type_t event_type, pbx_subscription_callback_t callback)
{
	ast_rwlock_rdlock(&event_map_lock);
	if (event_map[event_type].sub) {
		pbx_unsubscribe(event_type);
	} else {
		event_map[event_type].callback = callback;
		event_map[event_type].sub = ast_event_subscribe_new(event_map[event_type].ast_event_type, ast_event_cb, &event_type);
	}
	ast_rwlock_unlock(&event_map_lock);
	return 0;
}

int pbx_unsubscribe(pbx_event_type_t event_type)
{
	ast_rwlock_rdlock(&event_map_lock);
	if (!event_map[event_type].sub) {
		// not subscribed error
	} else {
		event_map[event_type].sub = ast_event_unsubscribe(event_map[event_type].sub);
		event_map[event_type].callback = NULL;
	}
	ast_rwlock_unlock(&event_map_lock);
	return 0;
}

int pbx_publish(pbx_event_type_t event_type, char *jsonmsgbuffer, size_t buf_len)
{
	return -1;
}

/*
 * private 
 */
/* copied from asterisk/event.c */
struct ast_event {  
        /*! Event type */ 
        enum ast_event_type type:16;
        /*! Total length of the event */
        uint16_t event_len:16;
        /*! The data payload of the event, made up of information elements */
        unsigned char payload[0];
} __attribute__((packed));

static const struct ie_map {
	enum ast_event_ie_pltype ie_pltype;
	const char *name;
} ie_maps[AST_EVENT_IE_TOTAL] = {
	[AST_EVENT_IE_NEWMSGS]             = { AST_EVENT_IE_PLTYPE_UINT, "NewMessages" },			// 0x0001
	[AST_EVENT_IE_OLDMSGS]             = { AST_EVENT_IE_PLTYPE_UINT, "OldMessages" },
	[AST_EVENT_IE_MAILBOX]             = { AST_EVENT_IE_PLTYPE_STR,  "Mailbox" },
	[AST_EVENT_IE_UNIQUEID]            = { AST_EVENT_IE_PLTYPE_UINT, "UniqueID" },
	[AST_EVENT_IE_EVENTTYPE]           = { AST_EVENT_IE_PLTYPE_UINT, "EventType" },
	[AST_EVENT_IE_EXISTS]              = { AST_EVENT_IE_PLTYPE_UINT, "Exists" },
	[AST_EVENT_IE_DEVICE]              = { AST_EVENT_IE_PLTYPE_STR,  "Device" },
	[AST_EVENT_IE_STATE]               = { AST_EVENT_IE_PLTYPE_UINT, "State" },
	[AST_EVENT_IE_CONTEXT]             = { AST_EVENT_IE_PLTYPE_STR,  "Context" },
	[AST_EVENT_IE_EID]                 = { AST_EVENT_IE_PLTYPE_RAW,  "EntityID" },
	[AST_EVENT_IE_CEL_EVENT_TYPE]      = { AST_EVENT_IE_PLTYPE_UINT, "CELEventType" },
	[AST_EVENT_IE_CEL_EVENT_TIME]      = { AST_EVENT_IE_PLTYPE_UINT, "CELEventTime" },
	[AST_EVENT_IE_CEL_EVENT_TIME_USEC] = { AST_EVENT_IE_PLTYPE_UINT, "CELEventTimeUSec" },
	[AST_EVENT_IE_CEL_USEREVENT_NAME]  = { AST_EVENT_IE_PLTYPE_UINT, "CELUserEventName" },
	[AST_EVENT_IE_CEL_CIDNAME]         = { AST_EVENT_IE_PLTYPE_STR,  "CELCIDName" },
	[AST_EVENT_IE_CEL_CIDNUM]          = { AST_EVENT_IE_PLTYPE_STR,  "CELCIDNum" },
	[AST_EVENT_IE_CEL_EXTEN]           = { AST_EVENT_IE_PLTYPE_STR,  "CELExten" },
	[AST_EVENT_IE_CEL_CONTEXT]         = { AST_EVENT_IE_PLTYPE_STR,  "CELContext" },
	[AST_EVENT_IE_CEL_CHANNAME]        = { AST_EVENT_IE_PLTYPE_STR,  "CELChanName" },
	[AST_EVENT_IE_CEL_APPNAME]         = { AST_EVENT_IE_PLTYPE_STR,  "CELAppName" },
	[AST_EVENT_IE_CEL_APPDATA]         = { AST_EVENT_IE_PLTYPE_STR,  "CELAppData" },
	[AST_EVENT_IE_CEL_AMAFLAGS]        = { AST_EVENT_IE_PLTYPE_STR,  "CELAMAFlags" },
	[AST_EVENT_IE_CEL_ACCTCODE]        = { AST_EVENT_IE_PLTYPE_UINT, "CELAcctCode" },
	[AST_EVENT_IE_CEL_UNIQUEID]        = { AST_EVENT_IE_PLTYPE_STR,  "CELUniqueID" },
	[AST_EVENT_IE_CEL_USERFIELD]       = { AST_EVENT_IE_PLTYPE_STR,  "CELUserField" },
	[AST_EVENT_IE_CEL_CIDANI]          = { AST_EVENT_IE_PLTYPE_STR,  "CELCIDani" },
	[AST_EVENT_IE_CEL_CIDRDNIS]        = { AST_EVENT_IE_PLTYPE_STR,  "CELCIDrdnis" },
	[AST_EVENT_IE_CEL_CIDDNID]         = { AST_EVENT_IE_PLTYPE_STR,  "CELCIDdnid" },
	[AST_EVENT_IE_CEL_PEER]            = { AST_EVENT_IE_PLTYPE_STR,  "CELPeer" },
	[AST_EVENT_IE_CEL_LINKEDID]        = { AST_EVENT_IE_PLTYPE_STR,  "CELLinkedID" },
	[AST_EVENT_IE_CEL_PEERACCT]        = { AST_EVENT_IE_PLTYPE_STR,  "CELPeerAcct" },
	[AST_EVENT_IE_CEL_EXTRA]           = { AST_EVENT_IE_PLTYPE_STR,  "CELExtra" },
	[AST_EVENT_IE_SECURITY_EVENT]      = { AST_EVENT_IE_PLTYPE_STR,  "SecurityEvent" },
	[AST_EVENT_IE_EVENT_VERSION]       = { AST_EVENT_IE_PLTYPE_UINT, "EventVersion" },
	[AST_EVENT_IE_SERVICE]             = { AST_EVENT_IE_PLTYPE_STR,  "Service" },
	[AST_EVENT_IE_MODULE]              = { AST_EVENT_IE_PLTYPE_STR,  "Module" },
	[AST_EVENT_IE_ACCOUNT_ID]          = { AST_EVENT_IE_PLTYPE_STR,  "AccountID" },
	[AST_EVENT_IE_SESSION_ID]          = { AST_EVENT_IE_PLTYPE_STR,  "SessionID" },
	[AST_EVENT_IE_SESSION_TV]          = { AST_EVENT_IE_PLTYPE_STR,  "SessionTV" },
	[AST_EVENT_IE_ACL_NAME]            = { AST_EVENT_IE_PLTYPE_STR,  "ACLName" },
	[AST_EVENT_IE_LOCAL_ADDR]          = { AST_EVENT_IE_PLTYPE_STR,  "LocalAddress" },
	[AST_EVENT_IE_REMOTE_ADDR]         = { AST_EVENT_IE_PLTYPE_STR,  "RemoteAddress" },
	[AST_EVENT_IE_EVENT_TV]            = { AST_EVENT_IE_PLTYPE_STR,  "EventTV" },
	[AST_EVENT_IE_REQUEST_TYPE]        = { AST_EVENT_IE_PLTYPE_STR,  "RequestType" },
	[AST_EVENT_IE_REQUEST_PARAMS]      = { AST_EVENT_IE_PLTYPE_STR,  "RequestParams" },
	[AST_EVENT_IE_AUTH_METHOD]         = { AST_EVENT_IE_PLTYPE_STR,  "AuthMethod" },
	[AST_EVENT_IE_SEVERITY]            = { AST_EVENT_IE_PLTYPE_STR,  "Severity" },
	[AST_EVENT_IE_EXPECTED_ADDR]       = { AST_EVENT_IE_PLTYPE_STR,  "ExpectedAddress" },
	[AST_EVENT_IE_CHALLENGE]           = { AST_EVENT_IE_PLTYPE_STR,  "Challenge" },
	[AST_EVENT_IE_RESPONSE]            = { AST_EVENT_IE_PLTYPE_STR,  "Response" },
	[AST_EVENT_IE_EXPECTED_RESPONSE]   = { AST_EVENT_IE_PLTYPE_STR,  "ExpectedResponse" },
	[AST_EVENT_IE_RECEIVED_CHALLENGE]  = { AST_EVENT_IE_PLTYPE_STR,  "ReceivedChallenge" },
	[AST_EVENT_IE_RECEIVED_HASH]       = { AST_EVENT_IE_PLTYPE_STR,  "ReceivedHash" },
	[AST_EVENT_IE_USING_PASSWORD]      = { AST_EVENT_IE_PLTYPE_UINT, "UsingPassword" },
	[AST_EVENT_IE_ATTEMPTED_TRANSPORT] = { AST_EVENT_IE_PLTYPE_STR,  "AttemptedTransport" },
	[AST_EVENT_IE_CACHABLE]            = { AST_EVENT_IE_PLTYPE_UINT, "Cachable" },
	[AST_EVENT_IE_PRESENCE_PROVIDER]   = { AST_EVENT_IE_PLTYPE_STR,  "PresenceProvider" },
	[AST_EVENT_IE_PRESENCE_STATE]      = { AST_EVENT_IE_PLTYPE_UINT, "PresenceState" },
	[AST_EVENT_IE_PRESENCE_SUBTYPE]    = { AST_EVENT_IE_PLTYPE_STR,  "PresenceSubtype" },
	[AST_EVENT_IE_PRESENCE_MESSAGE]    = { AST_EVENT_IE_PLTYPE_STR,  "PresenceMessage" },
};
/* end copy */

/* Fix: required because of broken _ast_event_str_to_ie_type implementation */
int fixed_ast_event_str_to_ie_type(const char *str, enum ast_event_ie_type *ie_type)
{
        int i;
        //for (i = 0; i < ARRAY_LEN(ie_maps); i++) {		// broken, should start at 0x0001
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
int message2json(char *msg, const size_t msg_len, const struct ast_event *event) 
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
int json2message(struct ast_event **eventref, enum ast_event_type event_type, const char *msg)
{
	int res = DECODING_ERROR;
	struct ast_event *event = *eventref;
	struct ast_eid eid;
	char *tokenstr = strdupa(msg);
	trim_char_bothends(tokenstr,0);

	char *entry = NULL;
	char *key = NULL;
	char *value = NULL;
	char delims[]=",";
	int cachable = 0;

//	if (!(event = ast_event_new(event_type, AST_EVENT_IE_END))) {		/* can't use this because it automatically adds my local EID to the new event */
//		return DECODING_ERROR;
//	}
        if (!(event = ast_calloc(1, sizeof(*event)))) {				/* resorting to local copy of ast_event structure :-( */
                return MALLOC_ERROR; 
        }
        event->type = htons(event_type);
        event->event_len = htons(sizeof(*event));
	
	ast_debug(1, "Decoding Msg2Event %s, content: '%s'\n", ast_event_get_type_name(event), tokenstr);
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
					        ast_event_append_ie_raw(&event, ie_type, &eid, sizeof(eid));
					} else {
						ast_event_append_ie_raw(&event, ie_type, value, strlen(value));
					}
					break;
			}
			/* realloc inside one of the append functions failed */
			if (!event) {
			        return DECODING_ERROR;
                        }
		}
		entry = strtok(NULL, delims);
	}

	if (!ast_event_get_ie_raw(event, AST_EVENT_IE_EID)) {
	         ast_event_append_eid(&event);
	}

	ast_debug(1, "decoded msg into event\n");
	*eventref = event;
	return OK + cachable;

failed:
	ast_event_destroy(event);
	return res;
}
