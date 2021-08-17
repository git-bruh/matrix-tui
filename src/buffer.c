/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "buffer.h"
#include <stdbool.h>
#include <stdlib.h>
#include <wctype.h>

static const size_t buffer_max = 2000;

struct buffer *buffer_alloc(void) {
	struct buffer *buffer = (struct buffer *)calloc(1, sizeof(struct buffer));

	if (!buffer) {
		return NULL;
	}

	buffer->buf = (uint32_t *)calloc(buffer_max, sizeof(uint32_t));

	if (!buffer->buf) {
		free(buffer);
		return NULL;
	}

	return buffer;
}

void buffer_free(struct buffer *buffer) {
	free(buffer->buf);
	free(buffer);
}

int buffer_add(struct buffer *buffer, uint32_t uc) {
	if ((buffer->len + 1) >= buffer_max) {
		return BUFFER_FAIL;
	}

	/* Insertion in between, move everything forward by 1 char. */
	if (buffer->buf[buffer->cur]) {
		for (size_t i = buffer->len; i > buffer->cur; i--) {
			buffer->buf[i] = buffer->buf[i - 1];
		}
	}

	buffer->len++;
	buffer->buf[buffer->cur++] = uc;

	return BUFFER_SUCCESS;
}

int buffer_left(struct buffer *buffer) {
	if (buffer->cur > 0) {
		buffer->cur--;

		return BUFFER_SUCCESS;
	}

	return BUFFER_FAIL;
}

int buffer_left_word(struct buffer *buffer) {
	if (buffer->cur > 0) {
		do {
			buffer->cur--;
		} while (buffer->cur > 0 &&
		         ((iswspace((wint_t)buffer->buf[buffer->cur])) ||
		          !(iswspace((wint_t)buffer->buf[buffer->cur - 1]))));

		return BUFFER_SUCCESS;
	}

	return BUFFER_FAIL;
}

int buffer_right(struct buffer *buffer) {
	if (buffer->cur < buffer->len) {
		buffer->cur++;

		return BUFFER_SUCCESS;
	}

	return BUFFER_FAIL;
}

int buffer_right_word(struct buffer *buffer) {
	if (buffer->cur < buffer->len) {
		do {
			buffer->cur++;
		} while (buffer->cur < buffer->len &&
		         !((iswspace((wint_t)buffer->buf[buffer->cur])) &&
		           !(iswspace((wint_t)buffer->buf[buffer->cur - 1]))));

		return BUFFER_SUCCESS;
	}

	return BUFFER_FAIL;
}

int buffer_delete(struct buffer *buffer) {
	if (buffer->cur > 0) {
		/* len > 0 as cur < len. */
		buffer->len--;

		/* Move everything back by 1 char. */
		for (size_t i = --buffer->cur; i < buffer->len; i++) {
			buffer->buf[i] = buffer->buf[i + 1];
		}

		return BUFFER_SUCCESS;
	}

	return BUFFER_FAIL;
}

int buffer_delete_word(struct buffer *buffer) {
	size_t original_cur = buffer->cur;

	if ((buffer_left_word(buffer)) == -1) {
		return BUFFER_FAIL;
	}

	size_t new_cur = buffer->cur;

	buffer->cur = original_cur;

	/* Maybe just realloc the array instead of iterating over the whole array
	 * multiple times. */
	for (size_t i = original_cur; i > new_cur; i--) {
		buffer_delete(buffer);
	}

	return BUFFER_SUCCESS;
}
