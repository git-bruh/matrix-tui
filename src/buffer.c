/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "buffer.h"
#include <stdlib.h>
#include <string.h>
#include <wctype.h>

/* We use a basic array instead of something like a linked list of small arrays
 * / gap buffer as pretty much all messages are small enough that array
 * insertion / deletion performance isn't an issue. */

static const size_t buffer_max = 2000;

int
buffer_init(struct buffer *buffer) {
	if (!(buffer->buf = calloc(buffer_max, sizeof(*buffer->buf)))) {
		return -1;
	}

	return 0;
}

void
buffer_finish(struct buffer *buffer) {
	free(buffer->buf);

	memset(buffer, 0, sizeof(*buffer));
}

int
buffer_add(struct buffer *buffer, uint32_t uc) {
	if ((buffer->len + 1) >= buffer_max) {
		return -1;
	}

	/* Insertion in between, move everything forward by 1 char. */
	if (buffer->buf[buffer->cur]) {
		for (size_t i = buffer->len; i > buffer->cur; i--) {
			buffer->buf[i] = buffer->buf[i - 1];
		}
	}

	buffer->len++;
	buffer->buf[buffer->cur++] = uc;

	return 0;
}

int
buffer_left(struct buffer *buffer) {
	if (buffer->cur > 0) {
		buffer->cur--;

		return 0;
	}

	return -1;
}

int
buffer_left_word(struct buffer *buffer) {
	if (buffer->cur > 0) {
		do {
			buffer->cur--;
		} while (buffer->cur > 0 &&
				 ((iswspace((wint_t) buffer->buf[buffer->cur])) ||
				  !(iswspace((wint_t) buffer->buf[buffer->cur - 1]))));

		return 0;
	}

	return -1;
}

int
buffer_right(struct buffer *buffer) {
	if (buffer->cur < buffer->len) {
		buffer->cur++;

		return 0;
	}

	return -1;
}

int
buffer_right_word(struct buffer *buffer) {
	if (buffer->cur < buffer->len) {
		do {
			buffer->cur++;
		} while (buffer->cur < buffer->len &&
				 !((iswspace((wint_t) buffer->buf[buffer->cur])) &&
				   !(iswspace((wint_t) buffer->buf[buffer->cur - 1]))));

		return 0;
	}

	return -1;
}

int
buffer_delete(struct buffer *buffer) {
	if (buffer->cur > 0) {
		/* len > 0 as cur < len. */
		buffer->len--;

		/* Move everything back by 1 char. */
		for (size_t i = --buffer->cur; i < buffer->len; i++) {
			buffer->buf[i] = buffer->buf[i + 1];
		}

		return 0;
	}

	return -1;
}

int
buffer_delete_word(struct buffer *buffer) {
	size_t original_cur = buffer->cur;

	if ((buffer_left_word(buffer)) == -1) {
		return -1;
	}

	size_t new_cur = buffer->cur;

	buffer->cur = original_cur;

	/* Maybe just realloc the array instead of iterating over the whole array
	 * multiple times. */
	for (size_t i = original_cur; i > new_cur; i--) {
		buffer_delete(buffer);
	}

	return 0;
}
