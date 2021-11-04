/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "cJSON.h"
#include "input.h"
#include "log.h"
#include "matrix.h"
#include <assert.h>
#include <curl/curl.h>
#include <langinfo.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

#define ERRLOG(cond, ...) (!(cond) ? (log_fatal(__VA_ARGS__), true) : false)

struct state {
	char *current_room;
	FILE *log_fp;
	struct matrix *matrix;
	struct input input;
};

enum { input_height = 5, sync_timeout = 1000 };

static void
redraw(struct state *state) {
	input_redraw(&state->input);
	tb_present();
}

static void
cleanup(struct state *state) {
	input_finish(&state->input);
	matrix_destroy(state->matrix);

	tb_shutdown();
	matrix_global_cleanup();

	if (state->log_fp) {
		fclose(state->log_fp);
	}
}

static void
input(struct state *state) {
	input_set_initial_cursor(&state->input);
	redraw(state);

	struct tb_event event = {0};

	while ((tb_poll_event(&event)) != TB_ERR) {
		switch (event.type) {
		case TB_EVENT_KEY:
			switch ((input_event(&event, &state->input))) {
			case INPUT_NOOP:
				break;
			case INPUT_GOT_SHUTDOWN:
				return;
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

static void
sync_cb(struct matrix *matrix, struct matrix_sync_response *response) {
	struct matrix_room room;

	while ((matrix_sync_next(response, &room)) == MATRIX_SUCCESS) {
		printf("Events for room (%s)\n", room.id);

		struct matrix_state_event sevent;

		while ((matrix_sync_next(&room, &sevent)) == MATRIX_SUCCESS) {
			switch (sevent.type) {
			case MATRIX_ROOM_MEMBER:
				printf("(%s) => Membership (%s)\n", sevent.member.base.sender,
					   sevent.member.membership);
				break;
			case MATRIX_ROOM_POWER_LEVELS:
				break;
			case MATRIX_ROOM_CANONICAL_ALIAS:
				printf("Canonical Alias => (%s)\n",
					   sevent.canonical_alias.alias
						   ? sevent.canonical_alias.alias
						   : "");
				break;
			case MATRIX_ROOM_CREATE:
				printf("Created => Creator (%s), Version (%s), Federate (%d)\n",
					   sevent.create.creator, sevent.create.room_version,
					   sevent.create.federate);
				break;
			case MATRIX_ROOM_JOIN_RULES:
				printf("Join Rule => (%s)\n", sevent.join_rules.join_rule);
				break;
			case MATRIX_ROOM_NAME:
				printf("Name => (%s)\n", sevent.name.name);
				break;
			case MATRIX_ROOM_TOPIC:
				printf("Topic => (%s)\n", sevent.topic.topic);
				break;
			case MATRIX_ROOM_AVATAR:
				printf("Avatar => (%s)\n", sevent.avatar.url);
				break;
			case MATRIX_ROOM_UNKNOWN_STATE:
				break;
			default:
				assert(0);
			}
		}

		struct matrix_timeline_event tevent;

		while ((matrix_sync_next(&room, &tevent)) == MATRIX_SUCCESS) {
			switch (tevent.type) {
			case MATRIX_ROOM_MESSAGE:
				printf("(%s) => (%s)\n", tevent.message.base.sender,
					   tevent.message.body);
				break;
			case MATRIX_ROOM_REDACTION:
				printf("(%s) redacted by (%s) for (%s)\n",
					   tevent.redaction.redacts, tevent.redaction.base.event_id,
					   tevent.redaction.reason ? tevent.redaction.reason : "");
				break;
			case MATRIX_ROOM_ATTACHMENT:
				printf("File (%s), URL (%s), Msgtype (%s), Size (%u)\n",
					   tevent.attachment.body, tevent.attachment.url,
					   tevent.attachment.msgtype, tevent.attachment.info.size);
				break;
			default:
				assert(0);
			}
		}

		struct matrix_ephemeral_event eevent;

		while ((matrix_sync_next(&room, &eevent)) == MATRIX_SUCCESS) {
			switch (eevent.type) {
			case MATRIX_ROOM_TYPING: {
				cJSON *user_id = NULL;

				cJSON_ArrayForEach(user_id, eevent.typing.user_ids) {
					char *uid = cJSON_GetStringValue(user_id);

					if (uid) {
						printf("(%s) => Typing\n", uid);
					}
				}
			} break;
			default:
				assert(0);
			}
		}
	}
}

int
main() {
	if (ERRLOG(setlocale(LC_ALL, ""), "Failed to set locale.") ||
		ERRLOG(strcmp("UTF-8", nl_langinfo(CODESET)) == 0,
			   "Locale is not UTF-8.")) {
		return EXIT_FAILURE;
	}

	struct state state = {0};

	if (!ERRLOG(state.log_fp = fopen(LOG_PATH, "w"),
				"Failed to open log file '" LOG_PATH "'.") &&
		!ERRLOG(tb_init() == TB_OK, "Failed to initialize termbox.") &&
		!ERRLOG(tb_set_input_mode(TB_INPUT_ALT) == TB_OK,
				"Failed to set input mode.") &&
		!ERRLOG(log_add_fp(state.log_fp, LOG_TRACE) == 0,
				"Failed to initialize logging callbacks.") &&
		!ERRLOG(matrix_global_init() == 0,
				"Failed to initialize matrix globals.") &&
		!ERRLOG(input_init(&state.input, input_height) == 0,
				"Failed to initialize input UI.") &&
		!ERRLOG(state.matrix = matrix_alloc(sync_cb, MXID, HOMESERVER, &state),
				"Failed to initialize libmatrix.") &&
		!ERRLOG(matrix_login(state.matrix, PASS, NULL, NULL) == MATRIX_SUCCESS,
				"Failed to login.")) {
		input(&state);

#if 0
		switch ((matrix_sync_forever(state.matrix, NULL, sync_timeout))) {
		case MATRIX_NOMEM:
			(void) ERRLOG(0, "Out of memory!");
			break;
		case MATRIX_CURL_FAILURE:
			(void) ERRLOG(0, "Lost connection to homeserver.");
			break;
		default:
			break;
		}
#endif

		cleanup(&state);
		return EXIT_SUCCESS;
	}

	cleanup(&state);
	return EXIT_FAILURE;
}
