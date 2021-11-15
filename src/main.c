/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "matrix.h"
#include "widgets.h"

#include <assert.h>
#include <cjson/cJSON.h>
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

#define ERRLOG(cond, ...)                                                      \
	(!(cond) ? (fprintf(stderr, __VA_ARGS__), true) : false)

struct state {
#ifndef NDEBUG
	bool cleaned_up;
#endif
	enum { INPUT = 0, TREE } active_widget;
	char *current_room;
	struct matrix *matrix;
	struct input input;
	struct treeview treeview;
};

enum { sync_timeout = 1000 };

static void
redraw(struct state *state) {
	tb_clear();
	input_redraw(&state->input);
	treeview_redraw(&state->treeview);
	tb_present();
}

static void
cleanup(struct state *state) {
#ifndef NDEBUG
	assert(!state->cleaned_up);
	state->cleaned_up = true;
#endif

	input_finish(&state->input);
	treeview_finish(&state->treeview);
	matrix_destroy(state->matrix);

	tb_shutdown();
	matrix_global_cleanup();

	memset(state, 0, sizeof(*state));
}

static void
ui_cb(enum widget_type type, struct widget_points *points, void *userp) {
	(void) userp;

	enum { input_height = 5 };

	int height = tb_height();
	int width = tb_width();

	switch (type) {
	case WIDGET_INPUT:
		points->x1 = 0;
		points->x2 = width;
		points->y1 = height - input_height;
		points->y2 = height;
		break;
	case WIDGET_TREEVIEW:
		points->x1 = 0;
		points->x2 = width;
		points->y1 = 0;
		points->y2 = height;
		break;
	default:
		break;
	}
}

static const char *
tree_string_cb(void *data) {
	return (char *) data;
}

static int
ui_init(struct state *state) {
	struct widget_callback cb = {
	  .userp = state,
	  .cb = ui_cb,
	};

	static char name[] = "Root";

	struct treeview_node *root
	  = treeview_node_alloc(name, tree_string_cb, NULL);

	if ((input_init(&state->input, cb)) == -1
		|| (treeview_init(&state->treeview, root, cb)) == -1) {
		treeview_node_destroy(root);
		return -1;
	}

	return 0;
}

static enum widget_error
handle_tree(struct state *state, struct tb_event *event) {
	assert(state->active_widget == TREE);
	static char hello[] = "Hello!";

	if (!event->key && event->ch) {
		switch (event->ch) {
		case 'd':
			return treeview_event(&state->treeview, TREEVIEW_DELETE);
		case 'h':
			{
				struct treeview_node *node
				  = treeview_node_alloc(hello, tree_string_cb, NULL);

				return treeview_event(
						 &state->treeview, TREEVIEW_INSERT_PARENT, node)
						== WIDGET_REDRAW
					   ? WIDGET_REDRAW
					   : (treeview_node_destroy(node), WIDGET_NOOP);
			}
		case 'n':
			{
				struct treeview_node *node
				  = treeview_node_alloc(hello, tree_string_cb, NULL);

				return treeview_event(&state->treeview, TREEVIEW_INSERT, node)
						== WIDGET_REDRAW
					   ? WIDGET_REDRAW
					   : (treeview_node_destroy(node), WIDGET_NOOP);
			}
		case ' ':
			return treeview_event(&state->treeview, TREEVIEW_EXPAND);
		default:
			break;
		}
	}

	switch (event->key) {
	case TB_KEY_TAB:
		return treeview_event(&state->treeview, TREEVIEW_EXPAND);
	case TB_KEY_ARROW_UP:
		return treeview_event(&state->treeview, TREEVIEW_UP);
	case TB_KEY_ARROW_DOWN:
		return treeview_event(&state->treeview, TREEVIEW_DOWN);
	default:
		break;
	}

	return WIDGET_NOOP;
}

static enum widget_error
handle_input(struct state *state, struct tb_event *event) {
	assert(state->active_widget == INPUT);

	if (!event->key && event->ch) {
		return input_handle_event(&state->input, INPUT_ADD, event->ch);
	}

	bool mod = (event->mod & TB_MOD_SHIFT);
	bool mod_enter = (event->mod & TB_MOD_ALT);

	switch (event->key) {
	case TB_KEY_ENTER:
		if (mod_enter) {
			return input_handle_event(&state->input, INPUT_ADD, '\n');
		}
		break;
	case TB_KEY_BACKSPACE:
	case TB_KEY_BACKSPACE2:
		return input_handle_event(
		  &state->input, mod ? INPUT_DELETE_WORD : INPUT_DELETE);
	case TB_KEY_ARROW_RIGHT:
		return input_handle_event(
		  &state->input, mod ? INPUT_RIGHT_WORD : INPUT_RIGHT);
	case TB_KEY_ARROW_LEFT:
		return input_handle_event(
		  &state->input, mod ? INPUT_LEFT_WORD : INPUT_LEFT);
	default:
		break;
	}

	return WIDGET_NOOP;
}

static void
ui_loop(struct state *state) {
	tb_set_input_mode(TB_INPUT_ALT);
	tb_set_output_mode(TB_OUTPUT_256);

	struct tb_event event = {0};

	redraw(state);

	state->active_widget = TREE;

	while ((tb_poll_event(&event)) != TB_ERR) {
		enum widget_error ret = WIDGET_NOOP;

		if (event.type == TB_EVENT_RESIZE) {
			redraw(state);
			continue;
		}

		if (event.key == TB_KEY_CTRL_C) {
			return;
		}

		switch (state->active_widget) {
		case INPUT:
			ret = handle_input(state, &event);
			break;
		case TREE:
			ret = handle_tree(state, &event);
			break;
		default:
			assert(0);
		}

		if (ret == WIDGET_REDRAW) {
			redraw(state);
		}
	}
}

static void
sync_cb(struct matrix *matrix, struct matrix_sync_response *response) {
	(void) matrix;

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
				  sevent.canonical_alias.alias ? sevent.canonical_alias.alias
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
			case MATRIX_ROOM_TYPING:
				{
					cJSON *user_id = NULL;

					cJSON_ArrayForEach(user_id, eevent.typing.user_ids) {
						char *uid = cJSON_GetStringValue(user_id);

						if (uid) {
							printf("(%s) => Typing\n", uid);
						}
					}
				}
				break;
			default:
				assert(0);
			}
		}
	}
}

int
main() {
	if (ERRLOG(setlocale(LC_ALL, ""), "Failed to set locale.\n")
		|| ERRLOG(strcmp("UTF-8", nl_langinfo(CODESET)) == 0,
		  "Locale is not UTF-8.\n")) {
		return EXIT_FAILURE;
	}

	struct state state = {0};

	if (!ERRLOG(
		  matrix_global_init() == 0, "Failed to initialize matrix globals.\n")
		&& !ERRLOG(ui_init(&state) == 0, "Failed to initialize UI.\n")
		&& !ERRLOG(
		  state.matrix = matrix_alloc(sync_cb, MXID, HOMESERVER, &state),
		  "Failed to initialize libmatrix.\n")
		&& !ERRLOG(
		  matrix_login(state.matrix, PASS, NULL, NULL) == MATRIX_SUCCESS,
		  "Failed to login.\n")
		&& !ERRLOG(tb_init() == TB_OK, "Failed to initialize termbox.\n")) {
		ui_loop(&state);

#if 0
		switch ((matrix_sync_forever(state.matrix, NULL, sync_timeout))) {
		case MATRIX_NOMEM:
			(void) ERRLOG(0, "Out of memory!\n");
			break;
		case MATRIX_CURL_FAILURE:
			(void) ERRLOG(0, "Lost connection to homeserver.\n");
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
