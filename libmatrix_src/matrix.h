#ifndef MATRIX_H
#define MATRIX_H
/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: LGPL-3.0-or-later */

#include <curl/curl.h>
#include <ev.h>
#include <stdint.h>

struct matrix;

struct matrix_callbacks {
	void (*on_login)(struct matrix *matrix, char *access_token, void *userp);
	/* void (*on_room_event)(struct matrix *matrix, struct matrix_event *event,
	 * void *userp); */
};

/* Must allocate enum + 1. */
enum {
	MATRIX_MXID_MAX = 255,
};

struct matrix *
matrix_alloc(struct ev_loop *loop, struct matrix_callbacks callbacks,
             const char *mxid, const char *homeserver, void *userp);
void
matrix_destroy(struct matrix *matrix);

int
matrix_login(struct matrix *matrix, const char *password,
             const char *device_id);
int
matrix_begin_sync(struct matrix *matrix, int timeout);
#endif /* !MATRIX_H */
