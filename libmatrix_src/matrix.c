#include "matrix-priv.h"
#include <stdlib.h>

int
matrix_global_init(void) {
	return (curl_global_init(CURL_GLOBAL_DEFAULT)) == CURLE_OK ? 0 : -1;
}

struct matrix *
matrix_alloc(const struct matrix_callbacks callbacks, const char *mxid,
			 const char *homeserver, void *userp) {
	size_t len_mxid = strlen(mxid);
	size_t len_homeserver = strlen(homeserver);

	if (len_mxid < 1 || len_mxid > MATRIX_MXID_MAX || len_homeserver < 1) {
		return NULL;
	}

	struct matrix *matrix = calloc(1, sizeof(*matrix));

	if (!matrix) {
		return NULL;
	}

	*matrix = (struct matrix){.cb = callbacks,
							  .homeserver = strdup(homeserver),
							  .mxid = strdup(mxid),
							  .userp = userp};

	if (!matrix->homeserver || !matrix->mxid)
		P matrix_destroy(matrix);
}
}

void
matrix_destroy(struct matrix *matrix) {
	if (!matrix) {
		return;
	}

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
