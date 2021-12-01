/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "matrix-priv.h"

/* For boolean evaluation */
#define STREQ(s1, s2) ((strcmp(s1, s2)) == 0)

/* Safely get an int from a cJSON object without overflows. */
static int
get_int(const cJSON *json, const char name[], int int_default) {
	double tmp = cJSON_GetNumberValue(cJSON_GetObjectItem(json, name));

	if (!(isnan(tmp))) {
		return matrix_double_to_int(tmp);
	}

	return int_default;
}

static cJSON *
get_array(const cJSON *const object, const char *const string) {
	cJSON *tmp = cJSON_GetObjectItem(object, string);

	/* Elements are located in the child member. */
	return tmp ? tmp->child : NULL;
}

static int
parse_summary(struct matrix_room_summary *summary, const cJSON *data) {
	if (!data) {
		return -1;
	}

	*summary = (struct matrix_room_summary) {
	  .joined_member_count = get_int(data, "m.joined_member_count", 0),
	  .invited_member_count = get_int(data, "m.invited_member_count", 0),
	  .heroes = cJSON_GetObjectItem(data, "m.heroes"),
	};

	return 0;
}

static int
parse_timeline(struct matrix_room_timeline *timeline, const cJSON *data) {
	if (!data) {
		return -1;
	}

	*timeline = (struct matrix_room_timeline) {
	  .prev_batch = GETSTR(data, "prev_batch"),
	  .limited = cJSON_IsTrue(cJSON_GetObjectItem(data, "limited")),
	};

	return 0;
}

int
matrix_sync_room_next(
  struct matrix_sync_response *response, struct matrix_room *room) {
	for (enum matrix_room_type type = 0; type < MATRIX_ROOM_MAX; type++) {
		cJSON *room_json = response->rooms[type];

		while (room_json) {
			*room = (struct matrix_room) {
			  .id = room_json->string,
			  .events
			  = {[MATRIX_EVENT_STATE]
= type != MATRIX_ROOM_INVITE
? get_array(
cJSON_GetObjectItem(room_json, "state"), "events")
 : get_array(
 cJSON_GetObjectItem(room_json, "invite_state"), "events"),
					[MATRIX_EVENT_TIMELINE] = get_array(
				  cJSON_GetObjectItem(room_json, "timeline"), "events"),
					[MATRIX_EVENT_EPHEMERAL] = get_array(
				  cJSON_GetObjectItem(room_json, "ephemeral"), "events")},
			  .type = type,
			};

			switch (type) {
			case MATRIX_ROOM_LEAVE:
			case MATRIX_ROOM_JOIN:
				parse_summary(
				  &room->summary, cJSON_GetObjectItem(room_json, "summary"));
				parse_timeline(
				  &room->timeline, cJSON_GetObjectItem(room_json, "timeline"));
				break;
			case MATRIX_ROOM_INVITE:
				parse_summary(
				  &room->summary, cJSON_GetObjectItem(room_json, "summary"));
				break;
			default:
				assert(0);
			};

			room_json = response->rooms[type] = room_json->next;

			if (room->id) {
				return 0;
			}
		}
	}

	return -1;
}

/* Assign the event type and compare in the same statement to reduce chance of
 * typos. */
#define TYPE(enumeration, string)                                              \
	(revent->type = (enumeration), (STREQ(revent->base.type, string)))

int
matrix_event_state_parse(
  struct matrix_state_event *revent, const matrix_json_t *event) {
	if (!revent || !event) {
		return -1;
	}

	bool is_valid = true;
	revent->state_key = GETSTR(event, "state_key");

	revent->base = (struct matrix_state_base) {
	  .origin_server_ts
	  = get_int(event, "origin_server_ts", 0), /* TODO time_t */
	  .event_id = GETSTR(event, "event_id"),
	  .sender = GETSTR(event, "sender"),
	  .type = GETSTR(event, "type"),
	};

	cJSON *content = NULL;

	if (!revent->base.origin_server_ts || !revent->base.event_id
		|| !revent->base.sender || !revent->base.type
		|| !(content = cJSON_GetObjectItem(event, "content"))) {
		return -1;
	}

	if (TYPE(MATRIX_ROOM_MEMBER, "m.room.member")) {
		revent->member = (struct matrix_room_member) {
		  .is_direct = cJSON_IsTrue(cJSON_GetObjectItem(content, "is_direct")),
		  .membership = GETSTR(content, "membership"),
		  .prev_membership
		  = GETSTR(cJSON_GetObjectItem(event, "prev_content"), "membership"),
		  .avatar_url = GETSTR(content, "avatar_url"),
		  .displayname = GETSTR(content, "displayname"),
		};

		is_valid = !!revent->state_key && !!revent->member.membership;
	} else if (TYPE(MATRIX_ROOM_POWER_LEVELS, "m.room.power_levels")) {
		const int default_power = 50;

		revent->power_levels = (struct matrix_room_power_levels) {
		  .ban = get_int(content, "ban", default_power),
		  .events_default
		  = get_int(content, "events_default", 0), /* Exception. */
		  .invite = get_int(content, "invite", default_power),
		  .kick = get_int(content, "kick", default_power),
		  .redact = get_int(content, "redact", default_power),
		  .state_default = get_int(content, "state_default", default_power),
		  .users_default
		  = get_int(content, "users_default", 0), /* Exception. */
		  .events = cJSON_GetObjectItem(content, "events"),
		  .notifications = cJSON_GetObjectItem(content, "notifications"),
		  .users = cJSON_GetObjectItem(content, "users"),
		};
	} else if (TYPE(MATRIX_ROOM_CANONICAL_ALIAS, "m.room.canonical_alias")) {
		revent->canonical_alias = (struct matrix_room_canonical_alias) {
		  .alias = GETSTR(content, "alias"),
		};
	} else if (TYPE(MATRIX_ROOM_CREATE, "m.room.create")) {
		cJSON *federate = cJSON_GetObjectItem(content, "federate");
		const char *version = GETSTR(content, "room_version");

		if (!version) {
			version = "1";
		}

		revent->create = (struct matrix_room_create) {
		  .federate = federate ? cJSON_IsTrue(federate)
							   : true, /* Federation is enabled if the key
										  doesn't exist. */
		  .creator = GETSTR(content, "creator"),
		  .room_version = version,
		};
	} else if (TYPE(MATRIX_ROOM_JOIN_RULES, "m.room.join_rules")) {
		revent->join_rules = (struct matrix_room_join_rules) {
		  .join_rule = GETSTR(content, "join_rule"),
		};

		is_valid = !!revent->join_rules.join_rule;
	} else if (TYPE(MATRIX_ROOM_NAME, "m.room.name")) {
		revent->name = (struct matrix_room_name) {
		  .name = GETSTR(content, "name"),
		};
	} else if (TYPE(MATRIX_ROOM_TOPIC, "m.room.topic")) {
		revent->topic = (struct matrix_room_topic) {
		  .topic = GETSTR(content, "topic"),
		};
	} else if (TYPE(MATRIX_ROOM_AVATAR, "m.room.avatar")) {
		cJSON *info = cJSON_GetObjectItem(content, "info");

		revent->avatar = (struct matrix_room_avatar){
		  .url = GETSTR(content, "url"),
		  .info =
			{
			  .size = get_int(info, "size", 0),
			  .mimetype = GETSTR(info, "mimetype"),
			},
		};
	} else {
		/* TODO unknown */
		is_valid = false;
	}

	if (is_valid) {
		return 0;
	}

	return -1;
}

int
matrix_event_timeline_parse(
  struct matrix_timeline_event *revent, const matrix_json_t *event) {
	if (!revent || !event) {
		return -1;
	}

	bool is_valid = false;

	revent->base = (struct matrix_room_base) {
	  .origin_server_ts
	  = get_int(event, "origin_server_ts", 0), /* TODO time_t */
	  .event_id = GETSTR(event, "event_id"),
	  .sender = GETSTR(event, "sender"),
	  .type = GETSTR(event, "type"),
	};

	cJSON *content = NULL;

	if (!revent->base.origin_server_ts || !revent->base.event_id
		|| !revent->base.sender || !revent->base.type
		|| !(content = cJSON_GetObjectItem(event, "content"))) {
		return -1;
	}

	if (TYPE(MATRIX_ROOM_MESSAGE, "m.room.message")) {
		revent->message = (struct matrix_room_message) {
		  .body = GETSTR(content, "body"),
		  .msgtype = GETSTR(content, "msgtype"),
		  .format = GETSTR(content, "format"),
		  .formatted_body = GETSTR(content, "formatted_body"),
		};

		is_valid = !!revent->message.body && !!revent->message.msgtype;

		cJSON *info = cJSON_GetObjectItem(content, "info");

		/* Check if the message is an attachment. */
		if (is_valid && info
			&& (STREQ(revent->message.msgtype, "m.image")
				|| STREQ(revent->message.msgtype, "m.file")
				|| STREQ(revent->message.msgtype, "m.audio")
				|| STREQ(revent->message.msgtype, "m.video"))) {
			revent->type = MATRIX_ROOM_ATTACHMENT;
			revent->attachment = (struct matrix_room_attachment) {
			  .body = GETSTR(content, "body"),
			  .msgtype = GETSTR(content, "msgtype"),
			  .url = GETSTR(content, "url"),
			  .filename = GETSTR(content, "filename"),
			  .info = {.size = get_int(info, "size", 0),
						.mimetype = GETSTR(info, "mimetype")},
			};

			is_valid = !!revent->attachment.body && !!revent->attachment.msgtype
					&& !!revent->attachment.url;
		}
	} else if (TYPE(MATRIX_ROOM_REDACTION, "m.room.redaction")) {
		revent->redaction = (struct matrix_room_redaction) {
		  .redacts = GETSTR(event, "redacts"),
		  .reason = GETSTR(content, "reason"),
		};

		is_valid = !!revent->redaction.redacts;
	}

	if (is_valid) {
		return 0;
	}

	return -1;
}

int
matrix_event_ephemeral_parse(
  struct matrix_ephemeral_event *revent, const matrix_json_t *event) {
	if (!revent || !event) {
		return -1;
	}

	bool is_valid = false;

	revent->base = (struct matrix_ephemeral_base) {
	  .type = GETSTR(event, "type"),
	  .room_id = GETSTR(event, "room_id"),
	};

	cJSON *content = cJSON_GetObjectItem(event, "content");

	if (!content) {
		return -1;
	}

	if (TYPE(MATRIX_ROOM_TYPING, "m.typing")) {
		revent->typing = (struct matrix_room_typing) {
		  .user_ids = cJSON_GetObjectItem(content, "user_ids"),
		};

		is_valid = !!revent->typing.user_ids;
	}

	if (is_valid) {
		return 0;
	}

	return -1;
}

#undef TYPE

int
matrix_sync_event_next(
  struct matrix_room *room, struct matrix_sync_event *revent) {
	if (!room || !revent) {
		return -1;
	}

	for (revent->type = 0; revent->type < MATRIX_EVENT_MAX; revent->type++) {
		bool done = false;

		matrix_json_t **json = &room->events[revent->type];
		revent->json = *json;

		while (!done && *json) {
			switch (revent->type) {
			case MATRIX_EVENT_STATE:
				done = ((matrix_event_state_parse(&revent->state, *json)) == 0);
				break;
			case MATRIX_EVENT_TIMELINE:
				done = ((matrix_event_timeline_parse(&revent->timeline, *json))
						== 0);

				/* Sometimes state events might pop up in the timeline. */
				if (!done
					&& (matrix_event_state_parse(&revent->state, *json)) == 0) {
					revent->type = MATRIX_EVENT_STATE;
					done = true;
				}
				break;
			case MATRIX_EVENT_EPHEMERAL:
				done
				  = ((matrix_event_ephemeral_parse(&revent->ephemeral, *json))
					 == 0);
				break;
			case MATRIX_EVENT_MAX:
				assert(0);
			}

			*json = (*json)->next;
		}

		if (done) {
			return 0;
		}
	}

	return -1;
}

int
matrix_dispatch_sync(struct matrix *matrix,
  const struct matrix_sync_callbacks *callbacks, const cJSON *sync) {
	if (!matrix || !callbacks || !sync) {
		return -1;
	}

	cJSON *rooms = cJSON_GetObjectItem(sync, "rooms");

	struct matrix_sync_response response = {
	  .next_batch = GETSTR(sync, "next_batch"),
	  .rooms =
		{
		  [MATRIX_ROOM_LEAVE] = get_array(rooms, "leave"),
		  [MATRIX_ROOM_JOIN] = get_array(rooms, "join"),
		  [MATRIX_ROOM_INVITE] = get_array(rooms, "invite"),
		},
	};

	callbacks->sync_cb(matrix, &response);

	return 0;
}
