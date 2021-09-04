/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "input.h"
#include "matrix.h"
#include <assert.h>
#include <ev.h>
#include <langinfo.h>
#include <locale.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if 1
#define MXID "@testuser:localhost"
#define HOMESERVER "http://127.0.0.1:8008"
#define PASS "0000000 072142 063162 026563 067543 072156 067562 005154 072542"
#else
#define MXID ""
#define HOMESERVER ""
#define PASS ""
#endif

struct state {
	struct input input;
	struct matrix *matrix;
};

static const int input_height = 5;

static void
redraw(struct state *state) {
	input_redraw(&state->input);
	tb_render();
}

static void
cleanup(struct state *state) {
	input_finish(&state->input);
	matrix_destroy(state->matrix);

	tb_shutdown();
	curl_global_cleanup();
}

static void
login_cb(struct matrix *matrix, char *access_token, void *userp) {}

static void
input_cb(EV_P_ ev_io *w, int revents) {
	(void) revents;

	struct state *state = (struct state *) w->data;

	struct tb_event event = {0};

	if ((tb_peek_event(&event, 0)) != -1) {
		switch (event.type) {
		case TB_EVENT_KEY:
			switch ((input_event(event, &state->input))) {
			case INPUT_NOOP:
				break;
			case INPUT_GOT_SHUTDOWN:
				ev_io_stop(EV_A_ w);
				ev_break(EV_A_ EVBREAK_ALL);

				cleanup(state);
				break;
			case INPUT_NEED_REDRAW:
				redraw(state);
				break;
			default:
				assert(0);
			}

			break;
		case TB_EVENT_RESIZE:
			redraw(state);
			break;
		default:
			break;
		}
	}
}

/* We catch SIGWINCH and resize + redraw ourselves since termbox sends the
 * TB_EVENT_RESIZE event only on the next input peek after getting SIGWINCH.
 * This is not feasible in our case since we poll stdin ourselves. This function
 * does NOT need to be async-signal safe as the signals are caught
 * by libev and sent to us synchronously. */
static void
sig_cb(struct ev_loop *loop, ev_signal *w, int revents) {
	(void) loop;
	(void) revents;

	tb_resize();

	redraw((struct state *) w->data);
}

int
main() {
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

	struct state state = {0};

	struct ev_loop *loop = EV_DEFAULT;

	if (!loop || (curl_global_init(CURL_GLOBAL_ALL)) != CURLE_OK ||
	    (input_init(&state.input, input_height)) == -1 ||
	    !(state.matrix = matrix_alloc(
			  loop, (struct matrix_callbacks){.on_login = login_cb}, MXID,
			  HOMESERVER, &state))) {
		if (loop) {
			ev_loop_destroy(loop);
		}

		cleanup(&state);

		return EXIT_FAILURE;
	}

	/* Set initial cursor. */
	tb_set_cursor(0, tb_height() - 1);
	tb_render();

	struct ev_io stdin_event = {.data = &state};
	struct ev_signal sig_event = {.data = &state};

	ev_io_init(&stdin_event, input_cb, STDIN_FILENO, EV_READ);
	ev_signal_init(&sig_event, sig_cb, SIGWINCH);

	ev_io_start(loop, &stdin_event);
	ev_signal_start(loop, &sig_event);

	if ((matrix_login(state.matrix, PASS, NULL)) == -1 ||
	    (matrix_begin_sync(state.matrix, 0)) == -1) {
		cleanup(&state);

		return EXIT_FAILURE;
	}

	ev_run(loop, 0);

	return EXIT_SUCCESS;
}
