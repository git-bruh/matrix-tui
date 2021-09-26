#pragma once
/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "buffer.h"
#include "termbox.h"

struct input {
	struct buffer buffer;
	int max_height;
	int line_off; /* The number of lines to skip when rendering the buffer. */
	int last_cur_line; /* The "line" where the cursor was placed in the last
						  draw, used to advance / decrement cur_y and line_off.
						*/
	int cur_y; /* y co-ordinate relative to the input field (not window). */
};

enum input_action {
	INPUT_GOT_SHUTDOWN = -2,
	INPUT_NOOP,
	INPUT_NEED_REDRAW,
};

int
input_init(struct input *input, int input_height);
void
input_finish(struct input *input);

void
input_redraw(struct input *input);
void
input_set_initial_cursor(struct input *input);
int
input_event(struct tb_event event, struct input *input);
