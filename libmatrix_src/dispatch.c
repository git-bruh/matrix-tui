#include "cJSON.h"
#include "matrix-priv.h"
#include <assert.h>
#include <math.h>
#include <stdlib.h>
/* TODO pass errors to callbacks. */

static void
dispatch_login(struct matrix *matrix, const char *resp) {
	cJSON *json = cJSON_Parse(resp);
	cJSON *token = cJSON_GetObjectItem(json, "access_token");

	char *access_token = cJSON_GetStringValue(token);

	if (access_token) {
		matrix_set_authorization(matrix, access_token);
	}

	matrix->cb.on_login(matrix, access_token, matrix->userp);

	cJSON_Delete(json);
}

static void
dispatch_state(struct matrix *matrix, const cJSON *events) {
	cJSON *event = NULL;

	cJSON_ArrayForEach(event, events) {
		cJSON *content = cJSON_GetObjectItem(event, "content");

		// const struct matrix_state_event matrix_event = {};
	}
}

static void
dispatch_timeline(struct matrix *matrix, const cJSON *events) {
	cJSON *event = NULL;

	cJSON_ArrayForEach(event, events) {
		cJSON *content = cJSON_GetObjectItem(event, "content");

		const struct matrix_timeline_event matrix_event = {
			.content = {.body = cJSON_GetStringValue(
							cJSON_GetObjectItem(content, "body")),
						.formatted_body = cJSON_GetStringValue(
							cJSON_GetObjectItem(content, "formatted_body"))},
			.sender =
				cJSON_GetStringValue(cJSON_GetObjectItem(event, "sender")),
			.type = cJSON_GetStringValue(cJSON_GetObjectItem(event, "type")),
			.event_id =
				cJSON_GetStringValue(cJSON_GetObjectItem(event, "event_id"))};
		/* TODO origin_server_ts, unsigned */

		if (matrix_event.content.body && matrix_event.sender &&
			matrix_event.type && matrix_event.event_id) {
			matrix->cb.on_timeline_event(matrix, &matrix_event, matrix->userp);
		}
	}
}

static void
dispatch_ephemeral(struct matrix *matrix, const cJSON *events) {
	cJSON *event = NULL;

	cJSON_ArrayForEach(event, events) {}
}

static int
room_init(struct matrix_room *matrix_room, const cJSON *room) {
	cJSON *summary = cJSON_GetObjectItem(room, "summary");

	cJSON *heroes = cJSON_GetObjectItem(summary, "m.heroes");

	double double_joined = cJSON_GetNumberValue(
		cJSON_GetObjectItem(summary, "m.joined_member_count"));
	double double_invited = cJSON_GetNumberValue(
		cJSON_GetObjectItem(summary, "m.invited_member_count"));

	size_t len_heroes = (size_t) cJSON_GetArraySize(heroes);

	*matrix_room = (struct matrix_room){
		.id = room->string,
		.summary =
			{
				.heroes =
					calloc(len_heroes, sizeof(*matrix_room->summary.heroes)),
				/* We must ensure that we don't cast NaN to an int. */
				.joined_member_count =
					(!(isnan(double_joined)) ? (int) double_joined
														: 0),
				.invited_member_count =
					(!(isnan(double_invited)) ? (int) double_invited
														 : 0),
			},
	};

	if (!matrix_room->summary.heroes) {
		return -1;
	}

	cJSON *hero = NULL;

	cJSON_ArrayForEach(hero, heroes) {
		char *str = NULL;

		if ((str = cJSON_GetStringValue(hero))) {
			matrix_room->summary.heroes[matrix_room->summary.len_heroes++] =
				str;
		}
	}

	return 0;
}

static void
room_finish(struct matrix_room *matrix_room) {
	free(matrix_room->summary.heroes);
}

static void
dispatch_sync(struct matrix *matrix, const char *resp) {
	if (!matrix->cb.on_dispatch_start || !matrix->cb.on_dispatch_end) {
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

	char *prev_batch =
		cJSON_GetStringValue(cJSON_GetObjectItem(json, "prev_batch"));
	char *next_batch =
		cJSON_GetStringValue(cJSON_GetObjectItem(json, "next_batch"));

	cJSON_ArrayForEach(room, rooms) {
		if (!room->string) {
			continue;
		}

		struct matrix_dispatch_info info = {
			.timeline =
				{
					.limited =
						cJSON_IsTrue(cJSON_GetObjectItem(room, "limited"))
							? true
							: false,
					.prev_batch = cJSON_GetStringValue(
						cJSON_GetObjectItem(room, "prev_batch")),
				},
			.prev_batch = prev_batch,
			.next_batch = next_batch,
		};

		if ((room_init(&info.room, room)) == -1) {
			continue;
		}

		matrix->cb.on_dispatch_start(matrix, &info, matrix->userp);
		room_finish(&info.room);

		if (matrix->cb.on_state_event) {
			dispatch_state(
				matrix, cJSON_GetObjectItem(cJSON_GetObjectItem(room, "state"),
											"events"));
		}

		if (matrix->cb.on_timeline_event) {
			dispatch_timeline(
				matrix, cJSON_GetObjectItem(
							cJSON_GetObjectItem(room, "timeline"), "events"));
		}

		if (matrix->cb.on_ephemeral_event) {
			dispatch_ephemeral(
				matrix, cJSON_GetObjectItem(
							cJSON_GetObjectItem(room, "ephemeral"), "events"));
		}

		matrix->cb.on_dispatch_end(matrix, matrix->userp);
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
