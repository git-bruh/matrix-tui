#include "cJSON.h"
#include "matrix-priv.h"
#include <assert.h>
#include <stdlib.h>

static void
dispatch_login(struct matrix *matrix, struct transfer *transfer) {
	char *access_token = NULL;

	cJSON *json = cJSON_Parse(transfer->mem.buf);

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

void
matrix_parse_and_dispatch(struct matrix *matrix, struct transfer *transfer) {
	if (!transfer->mem.buf) {
		return;
	}

	assert(transfer->type >= 0);
	assert(transfer->type <= MATRIX_NUM_TYPES);

	matrix_dispatch[transfer->type](matrix, transfer);
}

void (*const matrix_dispatch[MATRIX_NUM_TYPES + 1])(
	struct matrix *matrix, struct transfer *transfer) = {
	dispatch_login,
};
