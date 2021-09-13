#include "cJSON.h"
#include "matrix-priv.h"
#include <assert.h>
#include <stdlib.h>

/* TODO pass errors to callbacks. */

static void
dispatch_login(struct matrix *matrix, const char *resp) {
	cJSON *json = cJSON_Parse(resp);
	cJSON *token = cJSON_GetObjectItem(json, "access_token");

	char *access_token = token->valuestring;

	if (access_token) {
		matrix_set_authorization(matrix, access_token);
	}

	matrix->cb.on_login(matrix, access_token, matrix->userp);

	cJSON_Delete(json);
}

static void
parse_and_dispatch(struct matrix *matrix, const char *resp) {
	cJSON *json = cJSON_Parse(resp);

	if (!json) {
		return;
	}

	/* Returns NULL if the first argument is NULL. */
	cJSON *rooms =
		cJSON_GetObjectItem(cJSON_GetObjectItem(json, "rooms"), "join");
	cJSON *room = NULL;

	cJSON_ArrayForEach(room, rooms) {
		cJSON *events = cJSON_GetObjectItem(
			cJSON_GetObjectItem(room, "timeline"), "events");
		cJSON *event_json = NULL;

		cJSON_ArrayForEach(event_json, events) {}
	}

	cJSON_Delete(json);
}

void
matrix_dispatch_response(struct matrix *matrix, struct transfer *transfer) {
	if (!transfer->mem.buf) {
		return;
	}

	enum matrix_type type = transfer->type;
	const char *resp = transfer->mem.buf;

	if (type == MATRIX_SYNC) {
		parse_and_dispatch(matrix, resp);

		return;
	}

	assert(type >= 0);
	assert(type <= MATRIX_NUM_TYPES);

	matrix_dispatch[type](matrix, resp);
}

void (*const matrix_dispatch[MATRIX_NUM_TYPES + 1])(struct matrix *matrix,
                                                    const char *data) = {
	dispatch_login,
};
