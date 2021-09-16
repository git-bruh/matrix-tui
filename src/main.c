/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "input.h"
#include "log.h"
#include "matrix.h"
#include <assert.h>
#include <curl/curl.h>
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
	struct ev_loop *loop;
	struct input input;
	struct matrix *matrix;
	FILE *log_fp;
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

	fclose(state->log_fp);
}

static void
room_event_cb(struct matrix *matrix, const struct matrix_room_event *event,
              void *userp) {
	tb_string(0, 1, TB_DEFAULT, TB_DEFAULT, event->room_id);

	tb_render();
}

static void
login_cb(struct matrix *matrix, const char *access_token, void *userp) {
	tb_string(0, 0, TB_DEFAULT, TB_DEFAULT,
	          access_token ? access_token : "Failed to login!");

	if ((matrix_sync(matrix, 0)) == -1) {
		tb_string(0, 1, TB_DEFAULT, TB_DEFAULT, "Failed to start sync!");
	}

	tb_render();
}

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

static int
init_terminal(void) {
	if (!(setlocale(LC_ALL, "")) ||
	    (strcmp("UTF-8", nl_langinfo(CODESET))) != 0) {
		log_fatal("Locale is not UTF-8.");

		return -1;
	}

	switch ((tb_init())) {
	case TB_EUNSUPPORTED_TERMINAL:
		log_fatal("Unsupported terminal. Is TERM set ?");

		return -1;
	case TB_EFAILED_TO_OPEN_TTY:
		log_fatal("Failed to open TTY.");

		return -1;
	case TB_EPIPE_TRAP_ERROR:
		log_fatal("Failed to create pipe.");

		return -1;
	case 0:
		break;
	default:
		assert(0);
	}

	return 0;
}

static int
init_matrix_and_ui(struct state *state) {
	struct matrix_callbacks callbacks = {.on_login = login_cb,
	                                     .on_room_event = room_event_cb};

	bool err = false;

	if (!err && (err = (curl_global_init(CURL_GLOBAL_ALL != CURLE_OK)))) {
		log_fatal("Failed to initialize curl.");
	}

	if (!err && (err = ((input_init(&state->input, input_height)) == -1))) {
		log_fatal("Failed to initialize input.");
	}

	if (!err &&
	    (err = (!(state->matrix = matrix_alloc(state->loop, callbacks, MXID,
	                                           HOMESERVER, &state))))) {
		log_fatal("Failed to initialize libmatrix.");
	}

	return err ? -1 : 0;
}

int
main() {
	struct state state = {0};

	{
		const char *log_path = "/tmp/matrix-client.log";

		FILE *log_fp = fopen(log_path, "w");

		if (!log_fp) {
			log_fatal("Failed to open log file '%s'.", log_path);
			return EXIT_FAILURE;
		}

		if ((init_terminal()) == -1) {
			fclose(log_fp);
			return EXIT_FAILURE;
		}

		state.log_fp = log_fp;
	}

	if ((log_add_fp(state.log_fp, LOG_TRACE)) == -1) {
		log_fatal("Failed to initialize logging callback.");

		cleanup(&state);
		return EXIT_FAILURE;
	}

	bool err = (!(state.loop = EV_DEFAULT));

	if (err) {
		log_fatal("Failed to initialize event loop.");
	}

	err = err ? err : ((init_matrix_and_ui(&state)) == -1);

	if (err) {
		if (state.loop) {
			ev_loop_destroy(state.loop);
		}

		cleanup(&state);
		return EXIT_FAILURE;
	}

	input_set_initial_cursor(&state.input);
	redraw(&state);

	struct ev_io stdin_event = {.data = &state};
	struct ev_signal sig_event = {.data = &state};

	ev_io_init(&stdin_event, input_cb, STDIN_FILENO, EV_READ);
	ev_signal_init(&sig_event, sig_cb, SIGWINCH);

	ev_io_start(state.loop, &stdin_event);
	ev_signal_start(state.loop, &sig_event);

	if ((matrix_login(state.matrix, PASS, NULL)) == -1) {
		log_fatal("Failed to login.");

		ev_loop_destroy(state.loop);
		cleanup(&state);
		return EXIT_FAILURE;
	}

	ev_run(state.loop, 0);

	return EXIT_SUCCESS;
}
