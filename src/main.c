/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "input.h"
#include <assert.h>
#include <errno.h>
#include <langinfo.h>
#include <locale.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef INFTIM
#define INFTIM (-1)
#endif

static const int input_height = 5;

enum { FD_INPUT, NUM_FDS };

int peek_input(struct input *input) {
	struct tb_event event = {0};

	if ((tb_peek_event(&event, 0)) != -1) {
		switch (event.type) {
		case TB_EVENT_KEY:
			switch ((input_event(event, input))) {
			case INPUT_NOOP:
				break;
			case INPUT_GOT_SHUTDOWN:
				return -1;
			case INPUT_NEED_REDRAW:
				input_redraw(input);
				tb_render();

				break;
			default:
				assert(0);
			}

			break;
		case TB_EVENT_RESIZE:
			input_redraw(input);
			tb_render();

			break;
		default:
			break;
		}
	}

	return 0;
}

int main() {
	if (!(setlocale(LC_ALL, "")) ||
	    (strcmp("UTF-8", nl_langinfo(CODESET))) != 0) {
		return EXIT_FAILURE;
	}

	switch ((tb_init())) {
	case TB_EUNSUPPORTED_TERMINAL:
	case TB_EFAILED_TO_OPEN_TTY:
	case TB_EPIPE_TRAP_ERROR:
		return EXIT_FAILURE;
	case 0:
		break;
	default:
		assert(0);
	}

	struct input *input = input_alloc(input_height);

	if (!input) {
		tb_shutdown();
		return EXIT_FAILURE;
	}

	/* Set initial cursor. */
	tb_set_cursor(0, tb_height() - 1);
	tb_render();

	struct pollfd fds[NUM_FDS] = {0};

	fds[FD_INPUT].fd = STDIN_FILENO;
	fds[FD_INPUT].events = POLLIN;

	for (;;) {
		if ((poll(fds, NUM_FDS, INFTIM)) != -1) {
			if (fds[FD_INPUT].revents & POLLIN) {
				if ((peek_input(input)) == -1) {
					tb_shutdown();
					input_free(input);

					return EXIT_SUCCESS;
				}
			}
		} else if (errno != EAGAIN) {
			tb_shutdown();
			input_free(input);

			return EXIT_FAILURE;
		}
	}
}
