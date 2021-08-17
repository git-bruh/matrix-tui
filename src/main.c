/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "input.h"
#include <assert.h>
#include <langinfo.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>

static const int input_height = 5;

int main() {
	if (!(setlocale(LC_ALL, "")) ||
	    (strcmp("UTF-8", nl_langinfo(CODESET))) != 0) {
		return EXIT_FAILURE;
	}

	switch (tb_init()) {
	case TB_EUNSUPPORTED_TERMINAL:
	case TB_EFAILED_TO_OPEN_TTY:
	case TB_EPIPE_TRAP_ERROR:
		return EXIT_FAILURE;
	case 0:
		break;
	default:
		assert(0);
	}

	struct tb_event event = {0};

	struct input *input = input_alloc(input_height);

	if (!input) {
		tb_shutdown();
		return EXIT_FAILURE;
	}

	/* Set initial cursor. */
	tb_set_cursor(0, tb_height() - 1);
	tb_render();

	while ((tb_poll_event(&event)) != -1) {
		switch (event.type) {
		case TB_EVENT_KEY:
			switch ((input_event(event, input))) {
			case INPUT_NOOP:
				break;
			case INPUT_GOT_SHUTDOWN:
				tb_shutdown();
				input_free(input);

				return EXIT_SUCCESS;
			case INPUT_NEED_REDRAW:
				input_redraw(input);
				tb_render();

				break;
			default:
				assert(0);
			}
		case TB_EVENT_RESIZE:
			input_redraw(input);
			tb_render();
		default:
			break;
		}
	}

	tb_shutdown();
	input_free(input);

	return EXIT_SUCCESS;
}
