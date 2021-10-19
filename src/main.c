/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "input.h"
#include "log.h"
#include "matrix.h"
#include <assert.h>
#include <curl/curl.h>
#include <langinfo.h>
#include <locale.h>
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

#define LOG_PATH "/tmp/" CLIENT_NAME ".log"

struct state {
	char *current_room;
	FILE *log_fp;
	struct matrix *matrix;
	struct input input;
};

static const int input_height = 5;
static const unsigned sync_timeout = 1000;

static void
redraw(struct state *state) {
	input_redraw(&state->input);
	tb_render();
}

static void
cleanup(struct state *state) {
#if 0
	input_finish(&state->input);
#endif
	matrix_destroy(state->matrix);

#if 0
	tb_shutdown();
#endif
	matrix_global_cleanup();

	fclose(state->log_fp);
}

static bool
log_if_err(bool condition, const char *error) {
	if (!condition) {
		log_fatal("%s", error);
	}

	return !condition;
}

static bool
input(struct state *state) {
	struct tb_event event = {0};

	if ((tb_peek_event(&event, 0)) != -1) {
		switch (event.type) {
		case TB_EVENT_KEY:
			switch ((input_event(event, &state->input))) {
			case INPUT_NOOP:
				break;
			case INPUT_GOT_SHUTDOWN:
				return false;
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

	return true;
}

static void
sync_cb(struct matrix *matrix, struct matrix_sync_response *response) {
	printf("%p: %p\n", matrix, response);
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

#if 0
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
#endif

		state.log_fp = log_fp;
	}

	if (!(log_if_err(((log_add_fp(state.log_fp, LOG_TRACE)) == 0),
					 "Failed to initialize logging callbacks.")) &&
		!(log_if_err(((matrix_global_init()) == 0),
					 "Failed to initialize matrix globals.")) &&
#if 0
		!(log_if_err(((input_init(&state.input, input_height)) == 0),
					 "Failed to initialize input layer.")) &&
#endif
		!(log_if_err(
			(state.matrix = matrix_alloc(sync_cb, MXID, HOMESERVER, &state)),
			"Failed to initialize libmatrix."))) {
#if 0
		input_set_initial_cursor(&state.input);
		redraw(&state);
#endif

		if (!(log_if_err(
				((matrix_login(state.matrix, PASS, NULL)) == MATRIX_SUCCESS),
				"Failed to login."))) {
#if 0
			while ((input(&state))) {
				/* Loop until Ctrl+C */
			}
#endif
			switch ((matrix_sync_forever(state.matrix, sync_timeout))) {
			case MATRIX_NOMEM:
				log_fatal("Out of memory!");
				break;
			case MATRIX_CURL_FAILURE:
				log_fatal("Lost connection to homeserver.");
				break;
			default:
				break;
			}

			cleanup(&state);
			return EXIT_SUCCESS;
		}
	}

	cleanup(&state);
	return EXIT_FAILURE;
}
