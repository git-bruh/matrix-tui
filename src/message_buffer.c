/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "message_buffer.h"

#include "room_ds.h"
#include "stb_ds.h"

#include <assert.h>
#include <wctype.h>

struct buf_item {
	int padding; /* Padding for sender. */
	/* Start/End indices into the message body. */
	size_t start;
	size_t end;
	struct message *message;
};

enum { WIDTH_NEWLINE = 0 };

void
message_buffer_finish(struct message_buffer *buf) {
	if (buf) {
		arrfree(buf->buf);
		memset(buf, 0, sizeof(*buf));
	}
}

static bool
ch_can_split_word(uint32_t ch) {
	/* We split on spaces or punctuations (for wrapping). */
	if ((iswspace((wint_t) ch))) {
		return true;
	}

	switch ((wint_t) ch) {
#include "punctuation_marks.h"
		return true;
	default:
		return false;
	}
}

static int
find_word_start_end(
  const uint32_t *buf, size_t current, size_t len, size_t *start, size_t *end) {
	assert(buf);
	assert(start);
	assert(end);

	int tmp_width = 0;
	int width = 0;

	for (*start = current; *start > 0; (*start)--) {
		if ((ch_can_split_word(buf[*start - 1]))) {
			break;
		}

		widget_uc_sanitize(buf[*start - 1], &tmp_width);
		width += tmp_width;
	}

	for (*end = current; *end < len; (*end)++) {
		if ((ch_can_split_word(buf[*end]))) {
			break;
		}

		widget_uc_sanitize(buf[*end], &tmp_width);
		width += tmp_width;
	}

	return width;
}

static size_t
find_next_word_start(
  const uint32_t *buf, size_t current, size_t len, int x, int max_x) {
	assert(buf);

	size_t last_large_word_start = current;

	for (; current < len; current++) {
		int width = 0;

		if ((ch_can_split_word(widget_uc_sanitize(buf[current], &width)))
			|| (current + 1) == len) {
			last_large_word_start = current + 1;
		}

		if ((x += width) >= max_x || width == WIDTH_NEWLINE) {
			break;
		}
	}

	return last_large_word_start;
}

int
message_buffer_insert(struct message_buffer *buf, struct widget_points *points,
  struct message *message) {
	assert(buf);
	assert(points);
	assert(message);

	int padding = widget_str_width(message->sender) + widget_str_width("<> ");
	int start_x = points->x1 + padding + 1;

	if (start_x >= points->x2) {
		return -1;
	}

	if (buf->zeroed) {
		buf->zeroed = false;
		buf->last_points = *points;
	}

	int x = start_x;

	for (size_t i = 0, prev_end = i, len = arrlenu(message->body); i < len;
		 i++) {
		int width = 0;
		widget_uc_sanitize(message->body[i], &width);

		bool overflow = (x += width) >= points->x2;

		if (overflow || width == WIDTH_NEWLINE || (i + 1) == len) {
			if (overflow) {
				size_t word_start = 0;
				size_t word_end = 0;

				/* We could keep track of the words in-place but that gets
				 * pretty messy so we just find it on-demand. */
				int word_width = find_word_start_end(
				  message->body, i, len, &word_start, &word_end);

				if (word_width < (points->x2 - start_x)) {
					arrput(buf->buf, ((struct buf_item) {.padding = padding,
									   .start = prev_end,
									   .end = word_start,
									   .message = message}));

					size_t next_word_start = find_next_word_start(message->body,
					  word_end, len, start_x + word_width, points->x2);

					arrput(buf->buf, ((struct buf_item) {.padding = padding,
									   .start = word_start,
									   .end = next_word_start,
									   .message = message}));

					/* We must subtract 1 as i will be iterated after continue.
					 */
					i = next_word_start - 1;
					prev_end = next_word_start;
					x = start_x;

					continue;
				}
			}

			arrput(buf->buf, ((struct buf_item) {.padding = padding,
							   .start = prev_end,
							   .end = i + 1,
							   .message = message}));

			prev_end = i + 1;
			x = start_x;
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
	message_buffer_ensure_sane_scroll(buf);

	return 0;
}

void
message_buffer_zero(struct message_buffer *buf) {
	assert(buf);
	arrsetlen(buf->buf, 0);
	buf->zeroed = true;
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

	if ((arrlenu(buf->buf)) == 0) {
		return false;
	}

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
		{
			size_t len = arrlenu(buf->buf);

			if (len > 0) {
				assert(!buf->zeroed);

				if (buf->zeroed) {
					break;
				}

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

	size_t len = arrlenu(buf->buf);

	if (len == 0) {
		return;
	}

	assert(!buf->zeroed);

	buf->last_points = *points;

	int y = points->y2 - 1;

	for (size_t i = (len - buf->scroll); i > 0 && y > points->y1; i--, y--) {
		assert(i <= len);

		struct buf_item *item = &buf->buf[i - 1];

		assert(!item->message->redacted);

		if (item->start == 0) {
			int x = points->x1;

			x += widget_print_str(
			  x, y, points->x2, TB_DEFAULT, TB_DEFAULT, "<");
			x += widget_print_str(
			  x, y, points->x2, TB_DEFAULT, TB_DEFAULT, item->message->sender);
			x += widget_print_str(
			  x, y, points->x2, TB_DEFAULT, TB_DEFAULT, "> ");

			(void) x;

			/* Screen too small. */
			if (item->padding >= points->x2) {
				continue;
			}

			assert(x == item->padding);
		}

		assert((widget_points_in_bounds(points, item->padding, y)));

		int x = item->padding;
		int width = 0;

		for (size_t msg_index = item->start; msg_index < item->end;
			 msg_index++) {
			uint32_t uc
			  = widget_uc_sanitize(item->message->body[msg_index], &width);

			if (width == WIDTH_NEWLINE) {
				/* Newlines should only exist before a break,
				 * i.e. be the last character. */
				assert((msg_index + 1) == item->end);
				continue;
			}

			tb_set_cell(x, y, uc, TB_DEFAULT, TB_DEFAULT);
			x += width;
		}
	}
}