/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: LGPL-3.0-or-later */

#include "matrix.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t writemem(void *c, size_t s, size_t n, void *up) {
	// fprintf(stderr, "%.*s\n", s * n, c);

	return s * n;
}

int matrix_init(struct matrix *matrix, struct matrix_callbacks callbacks) {
	if (!(matrix->easy = curl_easy_init()) ||
	    !(matrix->multi = curl_multi_init())) {
		if (matrix->easy) {
			curl_easy_cleanup(matrix->easy);
		}

		return -1;
	}

	curl_easy_setopt(matrix->easy, CURLOPT_URL, "https://duckduckgo.com");
	curl_easy_setopt(matrix->easy, CURLOPT_WRITEFUNCTION, writemem);
	curl_easy_setopt(matrix->easy, CURLOPT_WRITEDATA, NULL);

	curl_multi_add_handle(matrix->multi, matrix->easy);

	return 0;
}

void matrix_finish(struct matrix *matrix) {
	if (matrix->multi) {
		curl_multi_remove_handle(matrix->multi, matrix->easy);
		curl_multi_cleanup(matrix->multi);
	}

	if (matrix->easy) {
		curl_easy_cleanup(matrix->easy);
	}

	memset(matrix, 0, sizeof(*matrix));
}

/* This is just for testing, will be replaced by a proper API like multi_socket
 * (Maybe with libevent aswell). */
int matrix_poll(struct matrix *matrix, struct curl_waitfd extra_fds[],
                unsigned int extra_nfds, int timeout_ms) {
	int nfds = 0;

	curl_multi_poll(matrix->multi, extra_fds, extra_nfds, timeout_ms, &nfds);

	return nfds;
}

int matrix_perform(struct matrix *matrix) {
	int still_running = 0;

	curl_multi_perform(matrix->multi, &still_running);

	if (still_running == 0) {
		curl_multi_remove_handle(matrix->multi, matrix->easy);
		curl_multi_add_handle(matrix->multi, matrix->easy);

		curl_multi_perform(matrix->multi, &still_running);
	}

	return 0;
}
