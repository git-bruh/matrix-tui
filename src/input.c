/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "input.h"
#include "stb_ds.h"
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
should_scroll(int x, int width) {
	return (x >= ((tb_width()) - width) || (should_forcebreak(width)));
}

/* Returns the number of times y was advanced. */
static int
adjust_xy(int width, int *x, int *y) {
	int original_y = *y;

	if ((should_scroll(*x, width))) {
		*x = 0;
		(*y)++;
	}

	/* Newline, already scrolled. */
	if ((should_forcebreak(width))) {
		return *y - original_y;
	}

	*x += width;

	/* We must accomodate for another character to move the cursor to the next
	 * line, which prevents us from adding an unreachable character. */
	if ((should_scroll(*x, ch_width))) {
		*x = 0;
		(*y)++;
	}

	return *y - original_y;
}

int
input_init(struct input *input, int input_height) {
	*input = (struct input){.max_height = input_height, .last_cur_line = 1};

	return 0;
}

void
input_finish(struct input *input) {
	arrfree(input->buf);
	memset(input, 0, sizeof(*input));
}

void
input_redraw(struct input *input) {
	tb_clear();

	size_t buf_len = arrlenu(input->buf);

	int cur_x = 0, cur_line = 1, lines = 1;

	{
		int x = 0, y = 0, width = 0;

		for (size_t written = 0; written < buf_len; written++) {
			uc_sanitize(input->buf[written], &width);

			lines += adjust_xy(width, &x, &y);

			if ((written + 1) == input->cur_buf) {
				cur_x = x;
				cur_line = lines;
			}
		}
	}

	{
		int cur_delta = cur_line - input->last_cur_line;

		/* We moved forward, add the delta to the cursor position and add the
		 * overflow to the line offset. */
		if (cur_delta > 0 &&
			(input->cur_y += cur_delta) > (input->max_height - 1)) {
			input->line_off += (input->cur_y - (input->max_height - 1));
			input->cur_y -= (input->cur_y - (input->max_height - 1));
		}
		/* We moved backward, subtract the delta from the cursor position and
		 * subtract the underflow from the line offset. */
		else if (cur_delta < 0 && (input->cur_y -= -cur_delta) < 0) {
			input->line_off -= -(input->cur_y);
			input->cur_y += -(input->cur_y);
		}
	}

	assert(input->line_off >= 0);
	assert(input->cur_y >= 0);
	assert(input->cur_y < input->max_height);
	assert(input->line_off < lines);

	if (!input->line_off) {
		/* Prevent overflow if the cursor if on the first line and input would
		 * take more than the available lines to represent. */
		lines = lines > input->max_height ? input->max_height : lines;
	} else {
		/* Don't write more lines than will be visible. */
		lines = (input->line_off + input->max_height) < lines
					? input->line_off + input->max_height
					: lines;
	}

	assert(input->line_off < lines);

	input->last_cur_line = cur_line;

	int width = 0, line = 0;

	size_t written = 0;

	uint32_t uc = 0;

	/* Calculate starting index. */
	for (int x = 0, y = 0; written < buf_len; written++) {
		if (line == input->line_off) {
			break;
		}

		uc_sanitize(input->buf[written], &width);

		line += adjust_xy(width, &x, &y);
	}

	int x = 0;
	int y = tb_height() - (input->line_off ? input->max_height : lines);

	tb_set_cursor(cur_x, y + input->cur_y);

	while (written < buf_len) {
		if (line >= lines) {
			break;
		}

		assert(y < tb_height());
		assert((tb_height() - y) <= input->max_height);

		uc = uc_sanitize(input->buf[written++], &width);

		/* Don't print newlines directly as they mess up the screen. */
		if (!should_forcebreak(width)) {
			tb_set_cell(x, y, uc, TB_DEFAULT, TB_DEFAULT);
		}

		line += adjust_xy(width, &x, &y);
	}
}

void
input_set_initial_cursor(struct input *input) {
	(void) input;

	tb_set_cursor(0, tb_height() - 1);
}

static enum input_error
buf_add(struct input *input, uint32_t ch) {
	if (((arrlenu(input->buf)) + 1) >= buf_max) {
		return INPUT_NOOP;
	}

	arrins(input->buf, input->cur_buf, ch);
	input->cur_buf++;

	return INPUT_NEED_REDRAW;
}

static enum input_error
buf_left(struct input *input) {
	if (input->cur_buf > 0) {
		input->cur_buf--;

		return INPUT_NEED_REDRAW;
	}

	return INPUT_NOOP;
}

static enum input_error
buf_leftword(struct input *input) {
	if (input->cur_buf > 0) {
		do {
			input->cur_buf--;
		} while (input->cur_buf > 0 &&
				 ((iswspace((wint_t) input->buf[input->cur_buf])) ||
				  !(iswspace((wint_t) input->buf[input->cur_buf - 1]))));

		return INPUT_NEED_REDRAW;
	}

	return INPUT_NOOP;
}

static enum input_error
buf_right(struct input *input) {
	if (input->cur_buf < arrlenu(input->buf)) {
		input->cur_buf++;

		return INPUT_NEED_REDRAW;
	}

	return INPUT_NOOP;
}

static enum input_error
buf_rightword(struct input *input) {
	size_t buf_len = arrlenu(input->buf);

	if (input->cur_buf < buf_len) {
		do {
			input->cur_buf++;
		} while (input->cur_buf < buf_len &&
				 !((iswspace((wint_t) input->buf[input->cur_buf])) &&
				   !(iswspace((wint_t) input->buf[input->cur_buf - 1]))));

		return INPUT_NEED_REDRAW;
	}

	return INPUT_NOOP;
}

static enum input_error
buf_del(struct input *input) {
	if (input->cur_buf > 0) {
		--input->cur_buf;

		arrdel(input->buf, input->cur_buf);

		return INPUT_NEED_REDRAW;
	}

	return INPUT_NOOP;
}

static enum input_error
buf_delword(struct input *input) {
	size_t original_cur = input->cur_buf;

	if ((buf_leftword(input)) == INPUT_NEED_REDRAW) {
		arrdeln(input->buf, input->cur_buf, original_cur - input->cur_buf);

		return INPUT_NEED_REDRAW;
	}

	return INPUT_NOOP;
}

enum input_error
input_event(struct tb_event event, struct input *input) {
	if (!event.key && event.ch) {
		return buf_add(input, event.ch);
	}

	switch (event.key) {
	case TB_KEY_SPACE:
		return buf_add(input, ' ');
	case TB_KEY_ENTER:
		if (event.mod & TB_MOD_ALT) {
			return buf_add(input, '\n');
		}

		return INPUT_NOOP;
	case TB_KEY_BACKSPACE:
	case TB_KEY_BACKSPACE2:
		if (event.mod & TB_MOD_ALT) {
			return buf_delword(input);
		}

		return buf_del(input);
	case TB_KEY_ARROW_RIGHT:
		if (event.mod & TB_MOD_ALT) {
			return buf_rightword(input);
		}

		return buf_right(input);
	case TB_KEY_ARROW_LEFT:
		if (event.mod & TB_MOD_ALT) {
			return buf_leftword(input);
		}

		return buf_left(input);
	case TB_KEY_CTRL_C:
		return INPUT_GOT_SHUTDOWN;
	default:
		return INPUT_NOOP;
	}
}
