#include "cJSON.h"
#include "matrix-priv.h"
#include <assert.h>
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
dispatch_state(struct matrix *matrix, struct matrix_room *room,
			   const cJSON *events) {}

static void
dispatch_timeline(struct matrix *matrix, struct matrix_room *room,
				  const cJSON *events) {
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
				cJSON_GetStringValue(cJSON_GetObjectItem(event, "event_id")),
			.room = room};

		if (matrix_event.content.body && matrix_event.sender &&
			matrix_event.type && matrix_event.event_id) {
			matrix->cb.on_timeline_event(matrix, &matrix_event, matrix->userp);
		}
	}
}

static void
dispatch_ephemeral(struct matrix *matrix, struct matrix_room *room,
				   const cJSON *events) {}

static void
dispatch_account_data(struct matrix *matrix, struct matrix_room *room,
					  const cJSON *events) {}

static void
dispatch_sync(struct matrix *matrix, const char *resp) {
	cJSON *json = cJSON_Parse(resp);

	if (!json) {
		return;
	}

	if (matrix->cb.on_account_data_event) {
		dispatch_account_data(
			matrix, NULL,
			cJSON_GetObjectItem(cJSON_GetObjectItem(json, "account_data"),
								"events"));
	}

	/* Returns NULL if the first argument is NULL. */
	cJSON *rooms =
		cJSON_GetObjectItem(cJSON_GetObjectItem(json, "rooms"), "join");
	cJSON *room = NULL;

	cJSON_ArrayForEach(room, rooms) {
		if (!room->string) {
			continue;
		}

		struct matrix_room matrix_room = {
			.id = room->string,
		};

		if (matrix->cb.on_state_event) {
			dispatch_state(matrix, &matrix_room,
						   cJSON_GetObjectItem(
							   cJSON_GetObjectItem(room, "state"), "events"));
		}

		if (matrix->cb.on_timeline_event) {
			dispatch_timeline(
				matrix, &matrix_room,
				cJSON_GetObjectItem(cJSON_GetObjectItem(room, "timeline"),
									"events"));
		}

		if (matrix->cb.on_ephemeral_event) {
			dispatch_ephemeral(
				matrix, &matrix_room,
				cJSON_GetObjectItem(cJSON_GetObjectItem(room, "ephemeral"),
									"events"));
		}

		if (matrix->cb.on_account_data_event) {
			dispatch_account_data(
				matrix, &matrix_room,
				cJSON_GetObjectItem(cJSON_GetObjectItem(room, "account_data"),
									"events"));
		}
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
