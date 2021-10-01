#include "cJSON.h"
#include "matrix-priv.h"
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
/* TODO pass errors to callbacks. */

/* Get the value of a key from an object. */
#define GETSTR(obj, key) (cJSON_GetStringValue(cJSON_GetObjectItem(obj, key)))

/* Safely get an int from a cJSON object without overflows. */
static int
get_int(const cJSON *json, const char name[], int int_default) {
	double tmp = cJSON_GetNumberValue(cJSON_GetObjectItem(json, name));

	if (!(isnan(tmp))) {
		return double_to_int(tmp);
	}

	return int_default;
}

#if 0
static time_t
get_timestamp(const cJSON *json, const char name[]) {
	/* TODO */
}
#endif

static void
dispatch_login(struct matrix *matrix, const char *resp) {
	if (!matrix->cb.login) {
		return;
	}

	cJSON *json = cJSON_Parse(resp);
	char *access_token = GETSTR(json, "access_token");

	if (access_token) {
		matrix_set_authorization(matrix, access_token);
	}

	matrix->cb.login(matrix, access_token, matrix->userp);

	cJSON_Delete(json);
}

static void
dispatch_typing(struct matrix *matrix, const cJSON *content) {
	if (!matrix->cb.typing) {
		return;
	}

	cJSON *user_ids = cJSON_GetObjectItem(content, "user_ids");

	if (user_ids) {
		matrix->cb.typing(matrix, (void *) user_ids, matrix->userp);
	}
}

static void
dispatch_ephemeral(struct matrix *matrix, const cJSON *events) {
	cJSON *event = NULL;

	cJSON_ArrayForEach(event, events) {
		cJSON *content = cJSON_GetObjectItem(event, "content");
		char *type = GETSTR(content, "type");

		if ((strcmp(type, "m.typing")) == 0) {
			dispatch_typing(matrix, content);
		}
	}
}

static void
dispatch_avatar(struct matrix *matrix, struct matrix_state_base *base,
				const cJSON *content) {
	if (!matrix->cb.avatar) {
		return;
	}

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

	matrix->cb.avatar(matrix, &avatar, matrix->userp);
}

static void
dispatch_topic(struct matrix *matrix, struct matrix_state_base *base,
			   const cJSON *content) {
	if (!matrix->cb.topic) {
		return;
	}

	struct matrix_room_topic topic = {.base = base,
									  .topic = GETSTR(content, "topic")};

	matrix->cb.topic(matrix, &topic, matrix->userp);
}

static void
dispatch_name(struct matrix *matrix, struct matrix_state_base *base,
			  const cJSON *content) {
	if (!matrix->cb.name) {
		return;
	}

	struct matrix_room_name name = {.base = base,
									.name = GETSTR(content, "name")};

	matrix->cb.name(matrix, &name, matrix->userp);
}

static void
dispatch_power_levels(struct matrix *matrix, struct matrix_state_base *base,
					  const cJSON *content) {
	if (!matrix->cb.power_levels) {
		return;
	}

	const unsigned default_power = 50;

	struct matrix_room_power_levels power_levels = {
		.base = base,
		.ban = get_int(content, "ban", default_power),
		.events_default =
			get_int(content, "events_default", 0), /* Exception. */
		.invite = get_int(content, "invite", default_power),
		.kick = get_int(content, "kick", default_power),
		.redact = get_int(content, "redact", default_power),
		.state_default = get_int(content, "state_default", default_power),
		.users_default = get_int(content, "users_default", 0), /* Exception. */
		.events = cJSON_GetObjectItem(content, "events"),
		.notifications = cJSON_GetObjectItem(content, "notifications"),
		.users = cJSON_GetObjectItem(content, "users"),
	};

	matrix->cb.power_levels(matrix, &power_levels, matrix->userp);
}

static void
dispatch_member(struct matrix *matrix, struct matrix_state_base *base,
				const cJSON *content, const cJSON *prev_content) {
	if (!matrix->cb.member) {
		return;
	}

	struct matrix_room_member member = {
		.base = base,
		.is_direct = cJSON_IsTrue(cJSON_GetObjectItem(content, "is_direct")),
		.membership = GETSTR(content, "membership"),
		.prev_membership = GETSTR(prev_content, "membership"),
		.avatar_url = GETSTR(content, "avatar_url"),
		.displayname = GETSTR(content, "displayname"),
	};

	if (member.membership) {
		matrix->cb.member(matrix, &member, matrix->userp);
	}
}

static void
dispatch_join_rules(struct matrix *matrix, struct matrix_state_base *base,
					const cJSON *content) {
	if (!matrix->cb.join_rules) {
		return;
	}

	struct matrix_room_join_rules join_rules = {
		.base = base,
		.join_rule = GETSTR(content, "join_rule"),
	};

	if (join_rules.join_rule) {
		matrix->cb.join_rules(matrix, &join_rules, matrix->userp);
	}
}

static void
dispatch_create(struct matrix *matrix, struct matrix_state_base *base,
				const cJSON *content) {
	if (!matrix->cb.room_create) {
		return;
	}

	cJSON *federate = cJSON_GetObjectItem(content, "federate");

	char default_version[] = "1";

	struct matrix_room_create room_create = {
		.base = base,
		.federate = federate ? cJSON_IsTrue(federate) : true,
		.creator = GETSTR(content, "creator"),
		.room_version = GETSTR(content, "room_version"),
	};

	if (!room_create.room_version) {
		room_create.room_version = default_version;
	}

	matrix->cb.room_create(matrix, &room_create, matrix->userp);
}

static void
dispatch_canonical_alias(struct matrix *matrix, struct matrix_state_base *base,
						 const cJSON *content) {
	if (!matrix->cb.canonical_alias) {
		return;
	}

	struct matrix_room_canonical_alias alias = {
		.base = base,
		.alias = GETSTR(content, "alias"),
	};

	matrix->cb.canonical_alias(matrix, &alias, matrix->userp);
}

static void
dispatch_unknown_state(struct matrix *matrix, struct matrix_state_base *base,
					   const cJSON *content, const cJSON *prev_content) {
	if (!matrix->cb.unknown_state) {
		return;
	}

	struct matrix_unknown_state unknown = {
		.base = base,
		.content = cJSON_PrintUnformatted(content),
		.prev_content = cJSON_PrintUnformatted(prev_content),
	};

	if (unknown.content) {
		matrix->cb.unknown_state(matrix, &unknown, matrix->userp);
	}

	free(unknown.content);
	free(unknown.prev_content);
}

static void
dispatch_state(struct matrix *matrix, const cJSON *events) {
	cJSON *event = NULL;

	cJSON_ArrayForEach(event, events) {
		/* XXX: There's a bit of duplication of the common fields here. */
		struct matrix_state_base base = {
			.origin_server_ts = get_int(event, "origin_server_ts", 0),
			.event_id = GETSTR(event, "event_id"),
			.sender = GETSTR(event, "sender"),
			.type = GETSTR(event, "type"),
			.state_key = GETSTR(event, "state_key"),
		};

		if (!base.origin_server_ts || !base.event_id || !base.sender ||
			!base.type || !base.state_key) {
			continue;
		}

		cJSON *content = cJSON_GetObjectItem(event, "content");

		if (!content) {
			continue;
		}

		if ((strcmp(base.type, "m.room.member")) == 0) {
			dispatch_member(matrix, &base, content,
							cJSON_GetObjectItem(event, "prev_content"));
		} else if ((strcmp(base.type, "m.room.power_levels")) == 0) {
			dispatch_power_levels(matrix, &base, content);
		} else if ((strcmp(base.type, "m.room.canonical_alias")) == 0) {
			dispatch_canonical_alias(matrix, &base, content);
		} else if ((strcmp(base.type, "m.room.create")) == 0) {
			dispatch_create(matrix, &base, content);
		} else if ((strcmp(base.type, "m.room.join_rules")) == 0) {
			dispatch_join_rules(matrix, &base, content);
		} else if ((strcmp(base.type, "m.room.name")) == 0) {
			dispatch_name(matrix, &base, content);
		} else if ((strcmp(base.type, "m.room.topic")) == 0) {
			dispatch_topic(matrix, &base, content);
		} else if ((strcmp(base.type, "m.room.avatar")) == 0) {
			dispatch_avatar(matrix, &base, content);
		} else {
			dispatch_unknown_state(matrix, &base, content,
								   cJSON_GetObjectItem(event, "prev_content"));
		}
	}
}

static void
dispatch_message(struct matrix *matrix, struct matrix_room_base *base,
				 const cJSON *content) {
	if (!matrix->cb.message) {
		return;
	}

	struct matrix_room_message message = {
		.base = base,
		.body = GETSTR(content, "body"),
		.msgtype = GETSTR(content, "msgtype"),
		.format = GETSTR(content, "format"),
		.formatted_body = GETSTR(content, "formatted_body"),
	};

	if (message.body && message.msgtype) {
		matrix->cb.message(matrix, &message, matrix->userp);
	}
}

/* XXX: We must take in an extra argument for the redacts key as it's not
 * present in the "content" of the event but at the base. */
static void
dispatch_redaction(struct matrix *matrix, struct matrix_room_base *base,
				   char *redacts, const cJSON *content) {
	if (!matrix->cb.redaction) {
		return;
	}

	struct matrix_room_redaction redaction = {
		.base = base,
		.redacts = redacts,
		.reason = GETSTR(content, "reason"),
	};

	if (redaction.redacts) {
		matrix->cb.redaction(matrix, &redaction, matrix->userp);
	}
}

static void
dispatch_attachment(struct matrix *matrix, struct matrix_room_base *base,
					const cJSON *content) {
	if (!matrix->cb.attachment) {
		return;
	}

	cJSON *info = cJSON_GetObjectItem(content, "info");

	struct matrix_room_attachment attachment = {
		.base = base,
		.body = GETSTR(content, "body"),
		.msgtype = GETSTR(content, "msgtype"),
		.url = GETSTR(content, "url"),
		.filename = GETSTR(content, "filename"),
		.info =
			{
				.size = get_int(info, "size", 0),
				.mimetype = GETSTR(info, "mimetype"),
			},
	};

	if (attachment.body && attachment.msgtype && attachment.url &&
		attachment.filename) {
		matrix->cb.attachment(matrix, &attachment, matrix->userp);
	}
}

static void
dispatch_timeline(struct matrix *matrix, const cJSON *events) {
	cJSON *event = NULL;

	cJSON_ArrayForEach(event, events) {
		struct matrix_room_base base = {
			.origin_server_ts = get_int(event, "origin_server_ts", 0),
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

		if ((strcmp(base.type, "m.room.message")) == 0) {
			dispatch_message(matrix, &base, content);
		} else if ((strcmp(base.type, "m.room.redaction")) == 0) {
			dispatch_redaction(matrix, &base, GETSTR(event, "redacts"),
							   content);
		} else if ((strcmp(base.type, "m.location") != 0)) {
			/* Assume that the event is an attachment. */
			dispatch_attachment(matrix, &base, content);
		}
	}
}

static void
dispatch_sync(struct matrix *matrix, const char *resp) {
	if (!matrix->cb.dispatch_start || !matrix->cb.dispatch_end) {
		return;
	}

	cJSON *json = cJSON_Parse(resp);

	if (!json) {
		return;
	}

	/* Returns NULL if the first argument is NULL. */
	cJSON *rooms =
		cJSON_GetObjectItem(cJSON_GetObjectItem(json, "rooms"), "join");
	cJSON *room = NULL;

	char *next_batch = GETSTR(json, "next_batch");

	if (!next_batch) {
		cJSON_Delete(json);

		return;
	}

	cJSON_ArrayForEach(room, rooms) {
		if (!room->string) {
			continue;
		}

		{
			cJSON *timeline = cJSON_GetObjectItem(room, "timeline");

			struct matrix_dispatch_info info = {
				.room =
					{
						.id = room->string,
						.summary =
							{
								.heroes = cJSON_GetObjectItem(
									cJSON_GetObjectItem(room, "summary"),
									"m.heroes"),
								.joined_member_count =
									get_int(room, "m.joined_member_count", 0),
								.invited_member_count =
									get_int(room, "m.invited_member_count", 0),
							},
					},
				.timeline =
					{
						.limited = cJSON_IsTrue(
									   cJSON_GetObjectItem(timeline, "limited"))
									   ? true
									   : false,
						.prev_batch = GETSTR(timeline, "prev_batch"),
					},
				.next_batch = next_batch,
			};

			matrix->cb.dispatch_start(matrix, &info, matrix->userp);
		}

		dispatch_state(
			matrix,
			cJSON_GetObjectItem(cJSON_GetObjectItem(room, "state"), "events"));

		dispatch_ephemeral(
			matrix, cJSON_GetObjectItem(cJSON_GetObjectItem(room, "ephemeral"),
										"events"));

		dispatch_timeline(
			matrix, cJSON_GetObjectItem(cJSON_GetObjectItem(room, "timeline"),
										"events"));

		matrix->cb.dispatch_end(matrix, matrix->userp);
	}

	cJSON_Delete(json);
}

void
matrix_dispatch_response(struct matrix *matrix, struct transfer *transfer) {
	const char *resp = transfer->mem.buf;

	if (!resp) {
		return;
	}

	switch (transfer->type) {
	case MATRIX_SYNC:
		dispatch_sync(matrix, resp);
		break;
	case MATRIX_LOGIN:
		dispatch_login(matrix, resp);
		break;
	default:
		assert(0);
	}
}
