/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "input.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

static const int ch_width = 2; /* Max width of a character. */

struct input {
	struct buffer *buffer;
	int max_height;
};

static uint32_t uc_sanitize(uint32_t uc, int *width) {
	int tmp_width = wcwidth((wchar_t)uc);

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

static bool should_forcebreak(int width) { return width == 0; }

static bool should_scroll(int x, int width) {
	return (x >= (tb_width() - width) || should_forcebreak(width));
}

static int adjust_xy(int width, int *x, int *y) {
	int original_y = *y;

	if (should_scroll(*x, width)) {
		*x = 0;
		(*y)++;
	}

	/* Newline, already scrolled. */
	if (should_forcebreak(width)) {
		return *y - original_y;
	}

	*x += width;

	/* We must accomodate for another character to move the cursor to the next
	 * line, which prevents us from adding an unreachable character. */
	if (should_scroll(*x, ch_width)) {
		*x = 0;
		(*y)++;
	}

	return *y - original_y;
}

struct input *input_create(int input_height) {
	struct input *input = (struct input *)calloc(1, sizeof(*input));

	if (!input) {
		return NULL;
	}

	input->buffer = buffer_create();

	if (!input->buffer) {
		free(input);

		return NULL;
	}

	input->max_height = input_height;

	return input;
}

void input_destroy(struct input *input) {
	buffer_destroy(input->buffer);
	free(input);
}

void input_redraw(struct input *input) {
	tb_clear_buffer();

	int cur_x = 0, cur_y = 0, cur_line = 0, line_start = 0, line_end = 0;

	/* Calculate needed lines as terminal height and width can vary. */
	{
		int x = 0, y = 0, width = 0, lines = 0;

		for (size_t i = 0; i < input->buffer->len; i++) {
			uc_sanitize(input->buffer->buf[i], &width);

			lines += adjust_xy(width, &x, &y);

			if ((i + 1) == input->buffer->cur) {
				cur_x = x;
				cur_line = lines;
			}
		}

		line_end = lines + 1; /* Count the first line aswell. */
	}

	int x = 0, y = 0;

	/* Calculate offsets. */
	{
		int off = line_end - input->max_height;

		/* off > 0 means the input will take more than the maximum lines
		 * available to represent. */
		y = tb_height() - (off > 0 ? input->max_height : line_end);

		line_start = (off > 0 ? off : 0);

		int cur_off = line_start - cur_line;

		if (cur_off > 0) {
			line_start -= cur_off;
			line_end -= cur_off;
		}

		cur_y = y + (cur_line - line_start);

		assert(cur_y >= y);
		assert(cur_y < tb_height());
	}

	assert(line_start >= 0);
	assert(line_end > line_start);

	int width = 0, line = 0;

	size_t written = 0;

	uint32_t uc = 0;

	/* Calculate starting index. */
	{
		int tmp_x = 0, tmp_y = 0;

		while (written < input->buffer->len) {
			if (line == line_start) {
				break;
			}

			uc_sanitize(input->buffer->buf[written++], &width);

			line += adjust_xy(width, &tmp_x, &tmp_y);
		}
	}

	/* Print the characters. */
	tb_set_cursor(cur_x, cur_y);

	for (; written < input->buffer->len; written++) {
		if (line >= line_end) {
			break;
		}

		assert(y < tb_height());
		assert((tb_height() - y) <= input->max_height);

		uc = uc_sanitize(input->buffer->buf[written], &width);

		/* Don't print newlines directly as they mess up the screen. */
		if (!should_forcebreak(width)) {
			tb_char(x, y, TB_DEFAULT, TB_DEFAULT, uc);
		}

		line += adjust_xy(width, &x, &y);
	}
}

int input_event(struct tb_event event, struct input *input) {
	if (!event.key && event.ch) {
		return buffer_add(input->buffer, event.ch);
	}

	switch (event.key) {
	case TB_KEY_SPACE:
		return buffer_add(input->buffer, ' ');
	case TB_KEY_ENTER:
		if (event.meta == TB_META_ALTCTRL) {
			return buffer_add(input->buffer, '\n');
		}

		return INPUT_NOOP;
	case TB_KEY_BACKSPACE:
		if (event.meta == TB_META_ALT) {
			return buffer_delete_word(input->buffer);
		}

		return buffer_delete(input->buffer);
	case TB_KEY_ARROW_RIGHT:
		if (event.meta == TB_META_CTRL) {
			return buffer_right_word(input->buffer);
		}

		return buffer_right(input->buffer);
	case TB_KEY_ARROW_LEFT:
		if (event.meta == TB_META_CTRL) {
			return buffer_left_word(input->buffer);
		}

		return buffer_left(input->buffer);
	case TB_KEY_CTRL_C:
		return INPUT_GOT_SHUTDOWN;
	default:
		return INPUT_NOOP;
	}
}
