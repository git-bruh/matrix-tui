#ifndef MATRIX_H
#define MATRIX_H
/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: LGPL-3.0-or-later */

#include <curl/curl.h>
#include <stdint.h>

struct matrix;

struct matrix_callbacks {
	void *ptr;
};

struct matrix *matrix_alloc(struct matrix_callbacks callbacks);
void matrix_destroy(struct matrix *matrix);
int matrix_poll(struct matrix *matrix, struct curl_waitfd extra_fds[],
                unsigned int extra_nfds, int timeout_ms);
int matrix_perform(struct matrix *matrix);
#endif /* !MATRIX_H */
