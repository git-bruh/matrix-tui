#ifndef MATRIX_H
#define MATRIX_H
/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: LGPL-3.0-or-later */

#include <curl/curl.h>
#include <ev.h>
#include <stdint.h>

struct matrix_callbacks {
	/* TODO */
	void *callback;
	void *another_callback;
};

struct matrix;

struct matrix *
matrix_alloc(struct ev_loop *loop);
void
matrix_destroy(struct matrix *matrix);
int
matrix_begin_sync(struct matrix *matrix, int timeout);
#endif /* !MATRIX_H */
