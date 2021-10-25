#ifndef INPUT_H
#define INPUT_H
/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "termbox.h"

struct input {
	int max_height;
	int line_off; /* The number of lines to skip when rendering the buffer. */
	int last_cur_line; /* The "line" where the cursor was placed in the last
						  draw, used to advance / decrement cur_y and line_off.
						*/
	int cur_y; /* y co-ordinate relative to the input field (not window). */
	size_t cur_buf; /* Current position inside buf. */
	uint32_t *buf;	/* We use a basic array instead of something like a linked
					 * list of small arrays  or a gap buffer as pretty much all
					 * messages are small enough that array
					 * insertion / deletion performance isn't an issue. */
};

enum input_error {
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

#endif /* !INPUT_H */
