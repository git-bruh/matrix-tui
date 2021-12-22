/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "message_buffer.h"

#include "room_ds.h"
#include "stb_ds.h"

#include <assert.h>

struct buf_item {
	int padding;  /* Padding for sender. */
	size_t start; /* Index to begin in the message body. */
	struct message *message;
};

void
message_buffer_finish(struct message_buffer *buf) {
	if (buf) {
		arrfree(buf->buf);
		memset(buf, 0, sizeof(*buf));
	}
}

int
message_buffer_insert(struct message_buffer *buf, struct widget_points *points,
  struct message *message) {
	assert(buf);
	assert(points);
	assert(message);

	int padding = widget_str_width(message->sender) + widget_str_width("<> ");
	int start_x = points->x1 + padding + 1;

	if (start_x > points->x2) {
		return -1;
	}

	int x = start_x;

	for (size_t i = 0, start = 0; message->body[i]; i++) {
		int width = 0;
		widget_uc_sanitize((uint32_t) message->body[i], &width);

		if ((x += width) >= points->x2 || width == 0 /* Newline */
			|| message->body[i + 1] == '\0') {
			arrput(buf->buf,
			  ((struct buf_item) {
				.padding = padding, .start = start, .message = message}));
			start = i;
			x = start_x + width; /* Account for start character. */
		}
	}

	return 0;
}

static int
cmp(const void *key, const void *array_item) {
	uint64_t i1 = *((const uint64_t *) key);
	uint64_t i2 = ((const struct buf_item *) array_item)->message->index;

	if (i1 > i2) {
		return 1;
	}

	if (i1 < i2) {
		return -1;
	}

	return 0; /* Equal. */
}

int
message_buffer_redact(struct message_buffer *buf, uint64_t index) {
	assert(buf);

	if (!buf->buf) {
		return -1;
	}

	size_t len = arrlenu(buf->buf);

	struct buf_item *message
	  = bsearch(&index, buf->buf, len, sizeof(*buf->buf), cmp);

	if (!message) {
		return -1;
	}

	size_t msg_index = (size_t) (message - buf->buf);
	assert(msg_index < len);

	/* Range of all the lines covered by this message. */
	size_t start = msg_index;
	size_t end = msg_index;

	for (size_t i = msg_index; i > 0; i--) {
		if (buf->buf[i - 1].message == message->message) {
			start = i - 1;
		} else {
			break;
		}
	}

	for (size_t i = msg_index; (i + 1) < len; i++) {
		if (buf->buf[i + 1].message == message->message) {
			end = i + 1;
		} else {
			break;
		}
	}

	assert(end < len);
	assert(end >= start);
	assert(start <= msg_index);
	assert(((end - start) + 1) <= len);

	arrdeln(buf->buf, start, (end - start) + 1);

	return 0;
}

void
message_buffer_zero(struct message_buffer *buf) {
	assert(buf);
	arrsetlen(buf->buf, 0);
	buf->points_valid = false;
}

void
message_buffer_ensure_sane_scroll(struct message_buffer *buf) {
	assert(buf);

	size_t len = arrlenu(buf->buf);

	if (len == 0) {
		buf->scroll = 0;
	} else if (buf->scroll >= len) {
		buf->scroll = len - 1;
	}
}

bool
message_buffer_should_recalculate(
  struct message_buffer *buf, struct widget_points *points) {
	assert(buf);
	assert(points);

	struct widget_points *cmp = &buf->last_points;

	/* No need to compare y axis since that doesn't impact how messages should
	 * be rendered, only the start and endpoint of the whole buffer. */
	return !(cmp->x1 == points->x1 && cmp->x2 == points->x2);
}

enum widget_error
message_buffer_handle_event(
  struct message_buffer *buf, enum message_buffer_event event, ...) {
	assert(buf);

	switch (event) {
	case MESSAGE_BUFFER_UP:
		if (buf->points_valid) {
			size_t len = arrlenu(buf->buf);

			if (len > 0) {
				int height = (buf->last_points.y2 - buf->last_points.y1);

				assert(height >= 0);
				assert((len - (buf->scroll + 1)) < len);

				/* Don't scroll unless we don't fit in the buffer. */
				if (len > ((size_t) height) && (len - (buf->scroll + 1)) > 0) {
					buf->scroll++;
					return WIDGET_REDRAW;
				}
			}
		}
		break;
	case MESSAGE_BUFFER_DOWN:
		if (buf->scroll > 0) {
			buf->scroll--;
			return WIDGET_REDRAW;
		}
		break;
	case MESSAGE_BUFFER_SELECT:
		break;
	}

	return WIDGET_NOOP;
}

void
message_buffer_redraw(
  struct message_buffer *buf, struct widget_points *points) {
	assert(buf);
	assert(points);

	if (buf->points_valid) {
		assert(!(message_buffer_should_recalculate(buf, points)));
	} else {
		buf->points_valid = true;
	}

	if ((arrlenu(buf->buf)) == 0) {
		return;
	}

	buf->last_points = *points;

	int y = points->y2 - 1;

	for (size_t len = arrlenu(buf->buf), i = (len - buf->scroll);
		 i > 0 && y > points->y1; i--, y--) {
		assert(i <= len);

		struct buf_item *item = &buf->buf[i - 1];

		if (item->start == 0) {
			int x = points->x1;

			x += widget_print_str(
			  x, y, points->x2, TB_DEFAULT, TB_DEFAULT, "<");
			x += widget_print_str(
			  x, y, points->x2, TB_DEFAULT, TB_DEFAULT, item->message->sender);
			x += widget_print_str(
			  x, y, points->x2, TB_DEFAULT, TB_DEFAULT, "> ");

			/* Screen too small. */
			if (item->padding >= points->x2) {
				continue;
			}

			assert(x == item->padding);
		}

		assert((widget_points_in_bounds(points, item->padding, y)));
		widget_print_str(item->padding, y, points->x2, TB_DEFAULT, TB_DEFAULT,
		  &item->message->body[item->start]);
	}
}
