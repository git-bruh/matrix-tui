/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "matrix-priv.h"

int
matrix_global_init(void) {
	return (curl_global_init(CURL_GLOBAL_DEFAULT)) == CURLE_OK ? 0 : -1;
}

struct matrix *
matrix_alloc(const char *mxid, const char *homeserver, void *userp) {
	{
		size_t len_mxid = 0;

		if (!mxid || !homeserver || (len_mxid = strlen(mxid)) < 1
			|| len_mxid > MATRIX_MXID_MAX || (strlen(homeserver)) < 1) {
			return NULL;
		}
	}

	struct matrix *matrix = malloc(sizeof(*matrix));

	if (matrix) {
		*matrix = (struct matrix) {.homeserver = strdup(homeserver),
		  .mxid = strdup(mxid),
		  .userp = userp};

		if (matrix->homeserver && matrix->mxid) {
			return matrix;
		}
	}

	matrix_destroy(matrix);
	return NULL;
}

void *
matrix_userp(struct matrix *matrix) {
	return matrix ? matrix->userp : NULL;
}

void
matrix_destroy(struct matrix *matrix) {
	if (!matrix) {
		return;
	}

	free(matrix->access_token);
	free(matrix->homeserver);
	free(matrix->mxid);
	free(matrix);
}

void
matrix_global_cleanup(void) {
	curl_global_cleanup();
}

void *
matrix_userdata(struct matrix *matrix) {
	return matrix->userp;
}

char *
matrix_json_print(matrix_json_t *json) {
	return cJSON_Print(json);
}
