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

/* Silence unused parameter warnings. */
#if EV_MULTIPLICITY
#define CAST_LOOP (void) EV_A
#else
#define CAST_LOOP (void) 0
#endif

#if 1
#define MXID "@testuser:localhost"
#define HOMESERVER "http://127.0.0.1:8008"
#define PASS "0000000 072142 063162 026563 067543 072156 067562 005154 072542"
#else
#define MXID ""
#define HOMESERVER ""
#define PASS ""
#endif

#define LOG_PATH "/tmp/" CLIENT_NAME ".log"

struct state {
	char *current_room;
	FILE *log_fp;
	struct ev_loop *loop;
	struct matrix *matrix;
	struct input input;
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
	ev_loop_destroy(state->loop);

	fclose(state->log_fp);
}

static bool
log_if_err(bool condition, const char *error) {
	if (!condition) {
		log_fatal("%s", error);
		return true;
	}

	return false;
}

static void
login_cb(struct matrix *matrix, const char *access_token, void *userp) {
	(void) matrix;
	(void) userp;

	tb_string(0, 0, TB_DEFAULT, TB_DEFAULT,
			  access_token ? access_token : "Failed to login!");

	if ((matrix_sync(matrix, 0)) == -1) {
		tb_string(0, 1, TB_DEFAULT, TB_DEFAULT, "Failed to start sync!");
	}

	tb_render();
}

static void
dispatch_start_cb(struct matrix *matrix,
				  const struct matrix_dispatch_info *info, void *userp) {
	(void) matrix;

	struct state *state = userp;

	if (!(state->current_room = strdup(info->room.id))) {
		return;
	}

	fprintf(stderr,
			"(%s) Timeline -> (limited = %d, prev_batch = %s)\n"
			"Global -> next_batch = %s\n--\n",
			state->current_room, info->timeline.limited,
			info->timeline.prev_batch, info->next_batch);
}

static void
timeline_cb(struct matrix *matrix, const struct matrix_timeline_event *event,
			void *userp) {
	(void) matrix;

	struct state *state = userp;

	if (!state->current_room) {
		return;
	}

	fprintf(stderr,
			"%s:\nBody: '%s'\nSender: '%s'\nType: '%s'\nEvent_id: '%s'\n",
			state->current_room, event->content.body, event->sender,
			event->type, event->event_id);
}

static void
dispatch_end_cb(struct matrix *matrix, void *userp) {
	(void) matrix;

	struct state *state = userp;

	free(state->current_room);
	state->current_room = NULL;
}

static void
input_cb(EV_P_ ev_io *w, int revents) {
	(void) revents;

	struct state *state = w->data;

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
sig_cb(EV_P_ ev_signal *w, int revents) {
	CAST_LOOP;
	(void) revents;

	tb_resize();

	redraw((struct state *) w->data);
}

int
main() {
	if ((log_if_err((setlocale(LC_ALL, "")), "Failed to set locale.")) ||
		(log_if_err(((strcmp("UTF-8", nl_langinfo(CODESET))) == 0),
					"Locale is not UTF-8."))) {
		return EXIT_FAILURE;
	}

	struct state state = {0};

	{
		FILE *log_fp = fopen(LOG_PATH, "w");

		if ((log_if_err((log_fp), "Failed to open log file '" LOG_PATH "'."))) {
			return EXIT_FAILURE;
		}

		bool success = false;

		switch ((tb_init())) {
		case TB_EUNSUPPORTED_TERMINAL:
			log_if_err((false), "Unsupported terminal. Is TERM set ?");
			break;
		case TB_EFAILED_TO_OPEN_TTY:
			log_if_err((false), "Failed to open TTY.");
			break;
		case TB_EPIPE_TRAP_ERROR:
			log_if_err((false), "Failed to create pipe.");
			break;
		case 0:
			success = true;
			break;
		default:
			assert(0);
		}

		if (!success) {
			fclose(log_fp);
			return EXIT_FAILURE;
		}

		state.log_fp = log_fp;
	}

	struct matrix_callbacks callbacks = {.on_login = login_cb,
										 .on_dispatch_start = dispatch_start_cb,
										 .on_timeline_event = timeline_cb,
										 .on_dispatch_end = dispatch_end_cb};

	if (!(log_if_err(((log_add_fp(state.log_fp, LOG_TRACE)) == 0),
					 "Failed to initialize logging callbacks.")) &&
		!(log_if_err((state.loop = EV_DEFAULT),
					 "Failed to initialize event loop.")) &&
		!(log_if_err(((curl_global_init(CURL_GLOBAL_ALL)) == CURLE_OK),
					 "Failed to initialize curl.")) &&
		!(log_if_err(((input_init(&state.input, input_height)) == 0),
					 "Failed to initialize input layer.")) &&
		!(log_if_err((state.matrix = matrix_alloc(state.loop, callbacks, MXID,
												  HOMESERVER, &state)),
					 "Failed to initialize libmatrix."))) {
		input_set_initial_cursor(&state.input);
		redraw(&state);

		struct ev_io stdin_event = {.data = &state};
		struct ev_signal sig_event = {.data = &state};

		ev_io_init(&stdin_event, input_cb, STDIN_FILENO, EV_READ);
		ev_signal_init(&sig_event, sig_cb, SIGWINCH);

		ev_io_start(state.loop, &stdin_event);
		ev_signal_start(state.loop, &sig_event);

		if (!(log_if_err(((matrix_login(state.matrix, PASS, NULL)) == 0),
						 "Failed to login."))) {
			ev_run(state.loop, 0); /* Blocks until completion. */

			cleanup(&state);
			return EXIT_SUCCESS;
		}
	}

	cleanup(&state);
	return EXIT_FAILURE;
}
