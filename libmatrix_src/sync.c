#include "matrix-priv.h"
#define STB_DS_IMPLEMENTATION
#include "stb_ds.h"

/* arrsetcap reassigns the array so arrlenu returns the new size. */
#define ENSURE(arr)                                                            \
	(((void) (arrsetcap(arr, arrlenu(arr) + 1)), arrlenu(arr) - 1))
#define ENSURELEN(arr, index) (arrsetlen(arr, index + 1))

const size_t evsize_initial = 10;

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

	cJSON *event = NULL;
	cJSON *events = cJSON_GetObjectItem(data, "events");

	arrsetcap(timeline->events.message, evsize_initial);
	arrsetcap(timeline->events.redaction, evsize_initial);
	arrsetcap(timeline->events.attachment, evsize_initial);

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
			size_t index = ENSURE(timeline->events.message);

			struct matrix_room_message *message =
				&timeline->events.message[index];

			*message = (struct matrix_room_message){
				.base = base,
				.body = GETSTR(content, "body"),
				.msgtype = GETSTR(content, "msgtype"),
				.format = GETSTR(content, "format"),
				.formatted_body = GETSTR(content, "formatted_body"),
			};

			/* Next iterations will fill the bad struct with new info. */
			if (message->body && message->msgtype) {
				ENSURELEN(timeline->events.message, index);
			}
		} else if (STRSAME(base.type, "m.room.redaction")) {
			size_t index = ENSURE(timeline->events.redaction);

			struct matrix_room_redaction *redaction =
				&timeline->events.redaction[index];

			*redaction = (struct matrix_room_redaction){
				.base = base,
				.redacts = GETSTR(event, "redacts"),
				.reason = GETSTR(content, "reason"),
			};

			if (redaction->redacts) {
				ENSURELEN(timeline->events.redaction, index);
			}
		} else if (STRSAME(base.type, "m.location")) {
			/* Assume that the event is an attachment. */
			size_t index = ENSURE(timeline->events.attachment);

			struct matrix_room_attachment *attachment =
				&timeline->events.attachment[index];

			cJSON *info = cJSON_GetObjectItem(content, "info");

			*attachment = (struct matrix_room_attachment){
				.base = base,
				.body = GETSTR(content, "body"),
				.msgtype = GETSTR(content, "msgtype"),
				.url = GETSTR(content, "url"),
				.filename = GETSTR(content, "filename"),
				.info = {.size = get_int(info, "size", 0),
						 .mimetype = GETSTR(info, "mimetype")},
			};

			if (attachment->body && attachment->msgtype && attachment->url &&
				attachment->filename) {
				ENSURELEN(timeline->events.attachment, index);
			}
		}
	}

	return 0;
}

static int
parse_account_data(struct matrix_account_data_events *account_data,
				   const cJSON *data) {
	return -1;
}

static int
parse_ephemeral(struct matrix_ephemeral_events *ephemeral, const cJSON *data) {
	if (!data) {
		return -1;
	}

	cJSON *event = NULL;
	cJSON *events = cJSON_GetObjectItem(data, "events");

	arrsetcap(ephemeral->typing, evsize_initial);

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
			size_t index = ENSURE(ephemeral->typing);

			struct matrix_room_typing *typing = &ephemeral->typing[index];

			*typing = (struct matrix_room_typing){
				.base = base,
				.user_ids = cJSON_GetObjectItem(content, "user_ids"),
			};

			if (typing->user_ids) {
				ENSURELEN(ephemeral->typing, index);
			}
		}
	}

	return 0;
}

static int
parse_state(struct matrix_state_events *state, const cJSON *data) {
	if (!data) {
		return -1;
	}

	arrsetcap(state->member, evsize_initial);
	arrsetcap(state->power_levels, evsize_initial);
	arrsetcap(state->canonical_alias, evsize_initial);
	arrsetcap(state->create, evsize_initial);
	arrsetcap(state->join_rules, evsize_initial);
	arrsetcap(state->name, evsize_initial);
	arrsetcap(state->topic, evsize_initial);
	arrsetcap(state->avatar, evsize_initial);
	arrsetcap(state->unknown, evsize_initial);

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
			size_t index = ENSURE(state->member);

			state->member[index] = (struct matrix_room_member){
				.base = base,
				.is_direct =
					cJSON_IsTrue(cJSON_GetObjectItem(content, "is_direct")),
				.membership = GETSTR(content, "membership"),
				.prev_membership = GETSTR(
					cJSON_GetObjectItem(event, "prev_content"), "membership"),
				.avatar_url = GETSTR(content, "avatar_url"),
				.displayname = GETSTR(content, "displayname"),
			};

			if (state->member[index].membership) {
				ENSURELEN(state->member, index);
			}
		} else if (STRSAME(base.type, "m.room.power_levels")) {
			size_t index = ENSURE(state->power_levels);

			const int default_power = 50;

			state->power_levels[index] = (struct matrix_room_power_levels){
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

			ENSURELEN(state->power_levels, index);
		} else if (STRSAME(base.type, "m.room.canonical_alias")) {
			size_t index = ENSURE(state->canonical_alias);

			state->canonical_alias[index] =
				(struct matrix_room_canonical_alias){
					.base = base,
					.alias = GETSTR(content, "alias"),
				};

			ENSURELEN(state->canonical_alias, index);
		} else if (STRSAME(base.type, "m.room.create")) {
			size_t index = ENSURE(state->create);

			cJSON *federate = cJSON_GetObjectItem(content, "federate");
			char *version = GETSTR(content, "room_version");

			if (!version) {
				version = "1";
			}

			state->create[index] = (struct matrix_room_create){
				.base = base,
				.federate = federate ? cJSON_IsTrue(federate)
									 : true, /* Federation is enabled if the key
												doesn't exist. */
				.creator = GETSTR(content, "creator"),
				.room_version = version,
			};

			ENSURELEN(state->create, index);
		} else if (STRSAME(base.type, "m.room.join_rules")) {
			size_t index = ENSURE(state->join_rules);

			state->join_rules[index] = (struct matrix_room_join_rules){
				.base = base,
				.join_rule = GETSTR(content, "join_rule"),
			};

			if (state->join_rules[index].join_rule) {
				ENSURELEN(state->join_rules, index);
			}
		} else if (STRSAME(base.type, "m.room.name")) {
			size_t index = ENSURE(state->name);

			state->name[index] = (struct matrix_room_name){
				.base = base,
				.name = GETSTR(content, "name"),
			};

			ENSURELEN(state->name, index);
		} else if (STRSAME(base.type, "m.room.topic")) {
			size_t index = ENSURE(state->topic);

			state->topic[index] = (struct matrix_room_topic){
				.base = base,
				.topic = GETSTR(content, "topic"),
			};

			ENSURELEN(state->topic, index);
		} else if (STRSAME(base.type, "m.room.avatar")) {
			size_t index = ENSURE(state->avatar);

			cJSON *info = cJSON_GetObjectItem(content, "info");

			state->avatar[index] = (struct matrix_room_avatar){
				.base = base,
				.url = GETSTR(content, "url"),
				.info =
					{
						.size = get_int(info, "size", 0),
						.mimetype = GETSTR(info, "mimetype"),
					},
			};

			ENSURELEN(state->avatar, index);
		} else {
			/* TODO unknown. */
		}
	}

	return 0;
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
			response.rooms.leave[index].id = room->string;
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
			response.rooms.join[index].id = room->string;
			parse_summary(&response.rooms.join[index].summary,
						  cJSON_GetObjectItem(room, "summary"));
			parse_timeline(&response.rooms.join[index].timeline,
						   cJSON_GetObjectItem(room, "timeline"));
			parse_account_data(&response.rooms.join[index].account_data,
							   cJSON_GetObjectItem(room, "account_data"));
			parse_ephemeral(&response.rooms.join[index].ephemeral,
							cJSON_GetObjectItem(room, "ephemeral"));
			parse_state(&response.rooms.join[index].state,
						cJSON_GetObjectItem(room, "state"));
			index++;
		}

		arrsetlen(response.rooms.join, index);
	}

	if ((cJSON_IsArray(invite))) {
		arrsetcap(response.rooms.invite, (size_t) cJSON_GetArraySize(invite));

		cJSON *room = NULL;
		size_t index = 0;

		cJSON_ArrayForEach(room, invite) {
			response.rooms.invite[index].id = room->string;
			parse_summary(&response.rooms.invite[index].summary,
						  cJSON_GetObjectItem(room, "summary"));
			parse_state(&response.rooms.invite[index].invite_state,
						cJSON_GetObjectItem(room, "invite_state"));
			index++;
		}

		arrsetlen(response.rooms.invite, index);
	}

	return 0;
}
