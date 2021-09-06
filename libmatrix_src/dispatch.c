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
		}

		cJSON_Delete(json);
	}

	matrix->access_token = access_token;
	matrix->cb.on_login(matrix, access_token, matrix->userp);
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
