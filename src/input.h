#ifndef INPUT_H
#define INPUT_H
/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "buffer.h"
#include "termbox.h"

struct input;

enum {
	INPUT_GOT_SHUTDOWN = -2,
	INPUT_NOOP,
	INPUT_NEED_REDRAW,
};

struct input *input_create(int input_height);
void input_destroy(struct input *input);

void input_redraw(struct input *input);
int input_event(struct tb_event event, struct input *input);
#endif /* !INPUT_H */
