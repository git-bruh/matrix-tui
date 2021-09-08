#include "cJSON.h"
#include "matrix-priv.h"
#include <assert.h>
#include <stdlib.h>

static void
dispatch_login(struct matrix *matrix, const char *resp) {
	char *access_token = NULL;

	cJSON *json = cJSON_Parse(resp);

	if (json) {
		cJSON *token = cJSON_GetObjectItem(json, "access_token");

		if (token) {
			access_token =
				token->valuestring ? strdup(token->valuestring) : NULL;

			const char auth[] = "Authorization: Bearer ";

			/* sizeof includes the NUL terminator required for the final string.
			 */
			size_t len_tmp = sizeof(auth) + strlen(access_token);

			char *header = calloc(len_tmp, sizeof(*header));

			if (header) {
				snprintf(header, len_tmp, "%s%s", auth, access_token);

				matrix_header_append(matrix, header);
			}

			free(header);
		}

		cJSON_Delete(json);
	}

	matrix->cb.on_login(matrix, access_token, matrix->userp);

	free(access_token);
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
