#include "matrix-priv.h"
#define STB_DS_IMPLEMENTATION
#include "stb_ds.h"

#define CALL(callback_name, data)                                              \
	do {                                                                       \
		if (matrix->cb.callback_name) {                                        \
			matrix->cb.callback_name(matrix, data);                            \
		}                                                                      \
	} while (0)

/* Safely get an int from a cJSON object without overflows. */
static int
get_int(const cJSON *json, const char name[], int int_default) {
	double tmp = cJSON_GetNumberValue(cJSON_GetObjectItem(json, name));

	if (!(isnan(tmp))) {
		return matrix_double_to_int(tmp);
	}

	return int_default;
}

static int
parse_summary(struct matrix_room_summary *summary, const cJSON *data) {
	if (!data) {
		return -1;
	}

	*summary = (struct matrix_room_summary){
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

	*timeline = (struct matrix_room_timeline){
		.prev_batch = GETSTR(data, "prev_batch"),
		.limited = cJSON_IsTrue(cJSON_GetObjectItem(data, "limited")),
	};

	return 0;
}

static void
dispatch_timeline(struct matrix *matrix, const cJSON *data) {
	if (!matrix || !data) {
		return;
	}

	cJSON *event = NULL;
	cJSON *events = cJSON_GetObjectItem(data, "events");

	cJSON_ArrayForEach(event, events) {
		struct matrix_room_base base = {
			.origin_server_ts =
				get_int(event, "origin_server_ts", 0), /* TODO time_t */
			.event_id = GETSTR(event, "event_id"),
			.sender = GETSTR(event, "sender"),
			.type = GETSTR(event, "type"),
		};

		if (!base.origin_server_ts || !base.event_id || !base.sender ||
			!base.type) {
			continue;
		}

		cJSON *content = cJSON_GetObjectItem(event, "content");

		if (STRSAME(base.type, "m.room.message")) {
			struct matrix_room_message message = {
				.base = base,
				.body = GETSTR(content, "body"),
				.msgtype = GETSTR(content, "msgtype"),
				.format = GETSTR(content, "format"),
				.formatted_body = GETSTR(content, "formatted_body"),
			};

			if (message.body && message.msgtype) {
				CALL(message, &message);
			}
		} else if (STRSAME(base.type, "m.room.redaction")) {
			struct matrix_room_redaction redaction = {
				.base = base,
				.redacts = GETSTR(event, "redacts"),
				.reason = GETSTR(content, "reason"),
			};

			if (redaction.redacts) {
				CALL(redaction, &redaction);
			}
		} else if (STRSAME(base.type, "m.location")) {
			/* Assume that the event is an attachment. */
			cJSON *info = cJSON_GetObjectItem(content, "info");

			struct matrix_room_attachment attachment = {
				.base = base,
				.body = GETSTR(content, "body"),
				.msgtype = GETSTR(content, "msgtype"),
				.url = GETSTR(content, "url"),
				.filename = GETSTR(content, "filename"),
				.info = {.size = get_int(info, "size", 0),
						 .mimetype = GETSTR(info, "mimetype")},
			};

			if (attachment.body && attachment.msgtype && attachment.url &&
				attachment.filename) {
				CALL(attachment, &attachment);
			}
		}
	}
}

static void
dispatch_state(struct matrix *matrix, const cJSON *data) {
	if (!matrix || !data) {
		return;
	}

	cJSON *event = NULL;
	cJSON *events = cJSON_GetObjectItem(data, "events");

	cJSON_ArrayForEach(event, events) {
		struct matrix_state_base base = {
			.origin_server_ts =
				get_int(event, "origin_server_ts", 0), /* TODO time_t */
			.event_id = GETSTR(event, "event_id"),
			.sender = GETSTR(event, "sender"),
			.type = GETSTR(event, "type"),
		};

		if (!base.origin_server_ts || !base.event_id || !base.sender ||
			!base.type) {
			continue;
		}

		cJSON *content = cJSON_GetObjectItem(event, "content");

		if (!content) {
			continue;
		}

		if (STRSAME(base.type, "m.room.member")) {
			struct matrix_room_member member = {
				.base = base,
				.is_direct =
					cJSON_IsTrue(cJSON_GetObjectItem(content, "is_direct")),
				.membership = GETSTR(content, "membership"),
				.prev_membership = GETSTR(
					cJSON_GetObjectItem(event, "prev_content"), "membership"),
				.avatar_url = GETSTR(content, "avatar_url"),
				.displayname = GETSTR(content, "displayname"),
			};

			if (member.membership) {
				CALL(member, &member);
			}
		} else if (STRSAME(base.type, "m.room.power_levels")) {
			const int default_power = 50;

			struct matrix_room_power_levels levels = {
				.base = base,
				.ban = get_int(content, "ban", default_power),
				.events_default =
					get_int(content, "events_default", 0), /* Exception. */
				.invite = get_int(content, "invite", default_power),
				.kick = get_int(content, "kick", default_power),
				.redact = get_int(content, "redact", default_power),
				.state_default =
					get_int(content, "state_default", default_power),
				.users_default =
					get_int(content, "users_default", 0), /* Exception. */
				.events = cJSON_GetObjectItem(content, "events"),
				.notifications = cJSON_GetObjectItem(content, "notifications"),
				.users = cJSON_GetObjectItem(content, "users"),
			};

			CALL(power_levels, &levels);
		} else if (STRSAME(base.type, "m.room.canonical_alias")) {
			struct matrix_room_canonical_alias alias = {
				.base = base,
				.alias = GETSTR(content, "alias"),
			};

			CALL(canonical_alias, &alias);
		} else if (STRSAME(base.type, "m.room.create")) {
			cJSON *federate = cJSON_GetObjectItem(content, "federate");
			char *version = GETSTR(content, "room_version");

			if (!version) {
				version = "1";
			}

			struct matrix_room_create create = {
				.base = base,
				.federate = federate ? cJSON_IsTrue(federate)
									 : true, /* Federation is enabled if the key
												doesn't exist. */
				.creator = GETSTR(content, "creator"),
				.room_version = version,
			};

			CALL(create, &create);
		} else if (STRSAME(base.type, "m.room.join_rules")) {
			struct matrix_room_join_rules join_rules = {
				.base = base,
				.join_rule = GETSTR(content, "join_rule"),
			};

			if (join_rules.join_rule) {
				CALL(join_rules, &join_rules);
			}
		} else if (STRSAME(base.type, "m.room.name")) {
			struct matrix_room_name name = {
				.base = base,
				.name = GETSTR(content, "name"),
			};

			CALL(name, &name);
		} else if (STRSAME(base.type, "m.room.topic")) {
			struct matrix_room_topic topic = {
				.base = base,
				.topic = GETSTR(content, "topic"),
			};

			CALL(topic, &topic);
		} else if (STRSAME(base.type, "m.room.avatar")) {
			cJSON *info = cJSON_GetObjectItem(content, "info");

			struct matrix_room_avatar avatar = {
				.base = base,
				.url = GETSTR(content, "url"),
				.info =
					{
						.size = get_int(info, "size", 0),
						.mimetype = GETSTR(info, "mimetype"),
					},
			};

			CALL(avatar, &avatar);
		} else {
			/* TODO unknown. CALL(unknown_state, &unknown); */
		}
	}
}

static void
dispatch_ephemeral(struct matrix *matrix, const cJSON *data) {
	if (!matrix || !data) {
		return;
	}

	cJSON *event = NULL;
	cJSON *events = cJSON_GetObjectItem(data, "events");

	cJSON_ArrayForEach(event, events) {
		struct matrix_ephemeral_base base = {
			.type = GETSTR(event, "type"),
			.room_id = GETSTR(event, "room_id"),
		};

		cJSON *content = cJSON_GetObjectItem(event, "content");

		if (!content) {
			continue;
		}

		if (STRSAME(base.type, "m.typing")) {
			struct matrix_room_typing typing = {
				.base = base,
				.user_ids = cJSON_GetObjectItem(content, "user_ids"),
			};

			if (typing.user_ids) {
				CALL(typing, &typing);
			}
		}
	}
}

void
dispatch_left_room(struct matrix *matrix, struct matrix_left_room *room) {
	if (!room) {
		return;
	}

	cJSON *events = room->events;

	dispatch_timeline(matrix, cJSON_GetObjectItem(events, "timeline"));
}

void
dispatch_joined_room(struct matrix *matrix, struct matrix_joined_room *room) {
	if (!room) {
		return;
	}

	cJSON *events = room->events;

	/* TODO account_data */
	dispatch_state(matrix, cJSON_GetObjectItem(events, "state"));
	dispatch_timeline(matrix, cJSON_GetObjectItem(events, "timeline"));
	dispatch_ephemeral(matrix, cJSON_GetObjectItem(events, "ephemeral"));
}

void
dispatch_invited_room(struct matrix *matrix, struct matrix_left_room *room) {
	if (!room) {
		return;
	}

	cJSON *events = room->events;

	dispatch_state(matrix, cJSON_GetObjectItem(events, "invite_state"));
}

int
matrix_dispatch_sync(const cJSON *sync) {
	if (!sync) {
		return -1;
	}

	cJSON *rooms = cJSON_GetObjectItem(sync, "rooms");

	struct matrix_sync_response response = {
		.next_batch = GETSTR(sync, "next_batch"),
	};

	cJSON *leave = cJSON_GetObjectItem(rooms, "leave");
	cJSON *join = cJSON_GetObjectItem(rooms, "join");
	cJSON *invite = cJSON_GetObjectItem(rooms, "invite");

	if ((cJSON_IsArray(leave))) {
		arrsetcap(response.rooms.leave, (size_t) cJSON_GetArraySize(leave));

		cJSON *room = NULL;
		size_t index = 0;

		cJSON_ArrayForEach(room, leave) {
			response.rooms.leave[index] = (struct matrix_left_room){
				.id = room->string,
				.events = room,
			};
			parse_summary(&response.rooms.leave[index].summary,
						  cJSON_GetObjectItem(room, "summary"));
			parse_timeline(&response.rooms.leave[index].timeline,
						   cJSON_GetObjectItem(room, "timeline"));
			index++;
		}

		arrsetlen(response.rooms.leave, index);
	}

	if ((cJSON_IsArray(join))) {
		arrsetcap(response.rooms.join, (size_t) cJSON_GetArraySize(join));

		cJSON *room = NULL;
		size_t index = 0;

		cJSON_ArrayForEach(room, join) {
			response.rooms.join[index] = (struct matrix_joined_room){
				.id = room->string,
				.events = room,
			};
			parse_summary(&response.rooms.join[index].summary,
						  cJSON_GetObjectItem(room, "summary"));
			parse_timeline(&response.rooms.join[index].timeline,
						   cJSON_GetObjectItem(room, "timeline"));
			index++;
		}

		arrsetlen(response.rooms.join, index);
	}

	if ((cJSON_IsArray(invite))) {
		arrsetcap(response.rooms.invite, (size_t) cJSON_GetArraySize(invite));

		cJSON *room = NULL;
		size_t index = 0;

		cJSON_ArrayForEach(room, invite) {
			response.rooms.invite[index] = (struct matrix_invited_room){
				.id = room->string,
				.events = room,
			};
			parse_summary(&response.rooms.invite[index].summary,
						  cJSON_GetObjectItem(room, "summary"));
			index++;
		}

		arrsetlen(response.rooms.invite, index);
	}

	return 0;
}
