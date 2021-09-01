#ifndef BUFFER_H
#define BUFFER_H
/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include <stdint.h>
#include <sys/types.h>

struct buffer {
	uint32_t *buf;
	size_t cur, len;
};

enum {
	BUFFER_FAIL = -1,
	BUFFER_SUCCESS,
};

int
buffer_init(struct buffer *buffer);
void
buffer_finish(struct buffer *buffer);

int
buffer_add(struct buffer *buffer, uint32_t uc);
int
buffer_left(struct buffer *buffer);
int
buffer_left_word(struct buffer *buffer);
int
buffer_right(struct buffer *buffer);
int
buffer_right_word(struct buffer *buffer);
int
buffer_delete(struct buffer *buffer);
int
buffer_delete_word(struct buffer *buffer);
#endif /* !BUFFER_H */
