/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "stb_ds.h"
#include "widgets.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

enum {
	buf_max = 2000,
	ch_width = 2, /* Max width of a character. */
};

static uint32_t
uc_sanitize(uint32_t uc, int *width) {
	int tmp_width = wcwidth((wchar_t) uc);

	switch (uc) {
	case '\n':
		*width = 0;
		return uc;
	case '\t':
		*width = 1;
		return ' ';
	default:
		if (tmp_width <= 0 || tmp_width > ch_width) {
			*width = 1;
			return '?';
		}

		*width = tmp_width;
		return uc;
	}
}

static bool
should_forcebreak(int width) {
	return width == 0;
}

static bool
should_scroll(int x, int width, int max_width) {
	return (x >= (max_width - width) || (should_forcebreak(width)));
}

/* Returns the number of times y was advanced. */
static int
adjust_xy(int width, struct widget_points *points, int *x, int *y) {
	int original_y = *y;

	if ((should_scroll(*x, width, points->x2))) {
		*x = points->x1;
		(*y)++;
	}

	/* Newline, already scrolled. */
	if ((should_forcebreak(width))) {
		return *y - original_y;
	}

	*x += width;

	/* We must accomodate for another character to move the cursor to the next
	 * line, which prevents us from adding an unreachable character. */
	if ((should_scroll(*x, ch_width, points->x2))) {
		*x = points->x1;
		(*y)++;
	}

	return *y - original_y;
}

static enum widget_error
buf_add(struct input *input, uint32_t ch) {
	if (((arrlenu(input->buf)) + 1) >= buf_max) {
		return WIDGET_NOOP;
	}

	arrins(input->buf, input->cur_buf, ch);
	input->cur_buf++;

	return WIDGET_REDRAW;
}

static enum widget_error
buf_left(struct input *input) {
	if (input->cur_buf > 0) {
		input->cur_buf--;

		return WIDGET_REDRAW;
	}

	return WIDGET_NOOP;
}

static enum widget_error
buf_leftword(struct input *input) {
	if (input->cur_buf > 0) {
		do {
			input->cur_buf--;
		} while (input->cur_buf > 0 &&
				 ((iswspace((wint_t) input->buf[input->cur_buf])) ||
				  !(iswspace((wint_t) input->buf[input->cur_buf - 1]))));

		return WIDGET_REDRAW;
	}

	return WIDGET_NOOP;
}

static enum widget_error
buf_right(struct input *input) {
	if (input->cur_buf < arrlenu(input->buf)) {
		input->cur_buf++;

		return WIDGET_REDRAW;
	}

	return WIDGET_NOOP;
}

static enum widget_error
buf_rightword(struct input *input) {
	size_t buf_len = arrlenu(input->buf);

	if (input->cur_buf < buf_len) {
		do {
			input->cur_buf++;
		} while (input->cur_buf < buf_len &&
				 !((iswspace((wint_t) input->buf[input->cur_buf])) &&
				   !(iswspace((wint_t) input->buf[input->cur_buf - 1]))));

		return WIDGET_REDRAW;
	}

	return WIDGET_NOOP;
}

static enum widget_error
buf_del(struct input *input) {
	if (input->cur_buf > 0) {
		--input->cur_buf;

		arrdel(input->buf, input->cur_buf);

		return WIDGET_REDRAW;
	}

	return WIDGET_NOOP;
}

static enum widget_error
buf_delword(struct input *input) {
	size_t original_cur = input->cur_buf;

	if ((buf_leftword(input)) == WIDGET_REDRAW) {
		arrdeln(input->buf, input->cur_buf, original_cur - input->cur_buf);

		return WIDGET_REDRAW;
	}

	return WIDGET_NOOP;
}

int
input_init(struct input *input, struct widget_callback cb) {
	*input = (struct input){.cb = cb};

	return 0;
}

void
input_finish(struct input *input) {
	if (!input) {
		return;
	}

	arrfree(input->buf);
	memset(input, 0, sizeof(*input));
}

void
input_redraw(struct input *input) {
	size_t buf_len = arrlenu(input->buf);

	struct widget_points points = {0};
	input->cb.cb(WIDGET_INPUT, &points, input->cb.userp);

	int max_height = points.y2 - points.y1;
	int cur_x = points.x1;
	int cur_line = 1;
	int lines = 1;

	{
		int x = points.x1;
		int y = 0;
		int width = 0;

		for (size_t written = 0; written < buf_len; written++) {
			uc_sanitize(input->buf[written], &width);

			lines += adjust_xy(width, &points, &x, &y);

			if ((written + 1) == input->cur_buf) {
				cur_x = x;
				cur_line = lines;
			}
		}
	}

	int diff_forward = cur_line - (input->start_y + max_height);
	int diff_backward = input->start_y - (cur_line - 1);

	if (diff_backward > 0) {
		input->start_y -= diff_backward;
	} else if (diff_forward > 0) {
		input->start_y += diff_forward;
	}

	assert(input->start_y >= 0);
	assert(input->start_y < lines);

	int width = 0;
	int line = 0;
	size_t written = 0;
	uint32_t uc = 0;

	/* Calculate starting index. */
	int y = points.y1;

	for (int x = points.x1; written < buf_len; written++) {
		if (line >= input->start_y) {
			break;
		}

		uc_sanitize(input->buf[written], &width);

		line += adjust_xy(width, &points, &x, &y);
	}

	int x = points.x1;

	tb_set_cursor(cur_x, points.y1 + (cur_line - (input->start_y + 1)));

	while (written < buf_len) {
		if (line >= lines || (y - input->start_y) >= points.y2) {
			break;
		}

		assert(x >= points.x1);
		assert(y >= points.y1);
		assert(x < points.x2);
		assert((y - input->start_y) >= points.y1);

		uc = uc_sanitize(input->buf[written++], &width);

		/* Don't print newlines directly as they mess up the screen. */
		if (!should_forcebreak(width)) {
			tb_set_cell(x, y - input->start_y, uc, TB_DEFAULT, TB_DEFAULT);
		}

		line += adjust_xy(width, &points, &x, &y);
	}
}

enum widget_error
input_handle_event(struct input *input, enum input_event event, ...) {
	switch (event) {
	case INPUT_DELETE:
		return buf_del(input);
	case INPUT_DELETE_WORD:
		return buf_delword(input);
	case INPUT_RIGHT:
		return buf_right(input);
	case INPUT_RIGHT_WORD:
		return buf_rightword(input);
	case INPUT_LEFT:
		return buf_left(input);
	case INPUT_LEFT_WORD:
		return buf_leftword(input);
	case INPUT_ADD: {
		va_list vl = {0};
		va_start(vl, event);
		uint32_t ch = va_arg(vl, uint32_t);
		va_end(vl);

		return buf_add(input, ch);
	}
	}

	return WIDGET_NOOP;
}
