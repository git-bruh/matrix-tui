/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "input.h"
#include "matrix.h"
#include <assert.h>
#include <langinfo.h>
#include <locale.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const int input_height = 5;

/* We catch SIGWINCH and set this variable as termbox's generates
 * TB_EVENT_RESIZE only on the next input peek after getting SIGWINCH. This is
 * not feasible in our case since we poll stdin ourselves. */
static volatile sig_atomic_t resize = false;

enum { FD_INPUT, NUM_FDS };

static int peek_input(struct input *input) {
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
			fprintf(stderr, "Got resiz\n");

			input_redraw(input);
			tb_render();

			break;
		default:
			break;
		}
	}

	return 0;
}

static void handle_signal(int sig) {
	(void)sig;

	resize = true;
}

int main() {
	if (!(setlocale(LC_ALL, "")) ||
	    (strcmp("UTF-8", nl_langinfo(CODESET))) != 0) {
		return EXIT_FAILURE;
	}

	if ((curl_global_init(CURL_GLOBAL_ALL)) != CURLE_OK) {
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

	struct sigaction action = {0};

	action.sa_handler = &handle_signal;
	sigaction(SIGWINCH, &action, NULL);

	struct input *input = input_create(input_height);

	if (!input) {
		tb_shutdown();
		return EXIT_FAILURE;
	}

	/* Set initial cursor. */
	tb_set_cursor(0, tb_height() - 1);
	tb_render();

	struct curl_waitfd fds[NUM_FDS] = {0};

	fds[FD_INPUT].fd = STDIN_FILENO;
	fds[FD_INPUT].events = CURL_WAIT_POLLIN;

	struct matrix_callbacks callbacks = {0};
	struct matrix *matrix = matrix_create(callbacks);

	matrix_perform(matrix);

	for (;;) {
		int nfds = matrix_poll(matrix, fds, NUM_FDS, 1000);

		if (resize) {
			resize = false;

			tb_resize();

			input_redraw(input);
			tb_render();
		}

		if ((nfds > 0 && (fds[FD_INPUT].revents & CURL_WAIT_POLLIN))) {
			if ((peek_input(input)) == -1) {
				tb_shutdown();

				input_destroy(input);
				matrix_destroy(matrix);

				curl_global_cleanup();

				return EXIT_SUCCESS;
			}

			nfds--;
		}

		/* Any remaining fds belong to curl. */
		if (nfds > 0) {
			matrix_perform(matrix);
		}
	}
}
