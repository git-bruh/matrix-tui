/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "matrix.h"
#include "queue.h"
#include "widgets.h"

#include <assert.h>
#include <cjson/cJSON.h>
#include <curl/curl.h>
#include <langinfo.h>
#include <locale.h>
#include <pthread.h>
#include <stdatomic.h>

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

enum { THREAD_SYNC = 0, THREAD_QUEUE, THREAD_MAX };

struct state {
	enum { INPUT = 0, TREE } active_widget;
	_Atomic bool done;
	char *current_room;
	pthread_t threads[THREAD_MAX];
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	struct matrix *matrix;
	struct queue queue;
	struct input input;
	struct treeview treeview;
};

struct queue_item {
	enum queue_item_type { QUEUE_ITEM_COMMAND = 0, QUEUE_ITEM_MAX } type;
	void *data;
};

void
handle_command(struct state *state, void *data) {
	char *buf = data;
	fprintf(stderr, "%s\n", buf);
	free(buf);
}

static void (*queue_item_cb[QUEUE_ITEM_MAX])(struct state *, void *) = {
  [QUEUE_ITEM_COMMAND] = handle_command,
};

static struct queue_item *
queue_item_alloc(enum queue_item_type type, void *data) {
	struct queue_item *item = (type < QUEUE_ITEM_MAX && data)
							  ? malloc(sizeof(struct queue_item))
							  : NULL;

	if (item) {
		*item = (struct queue_item) {
		  .type = type,
		  .data = data,
		};
	}

	return item;
}

static void
redraw(struct state *state) {
	tb_clear();

	int height = tb_height();
	int width = tb_width();

	enum { input_height = 5 };
	struct widget_points points = {0};

	widget_points_set(&points, 0, width, height - input_height, height);
	input_redraw(&state->input, &points);

	widget_points_set(&points, 0, width, 0, height);
	treeview_redraw(&state->treeview, &points);

	tb_present();
}

static void
cleanup(struct state *state) {
	input_finish(&state->input);
	treeview_finish(&state->treeview);
	tb_shutdown();

	state->done = true;

	if (state->threads[THREAD_SYNC]) {
		matrix_sync_cancel(state->matrix);
		pthread_join(state->threads[THREAD_SYNC], NULL);
	}

	if (state->threads[THREAD_QUEUE]) {
		pthread_cond_signal(&state->cond);
		pthread_join(state->threads[THREAD_QUEUE], NULL);
	}

	pthread_cond_destroy(&state->cond);
	pthread_mutex_destroy(&state->mutex);

	matrix_destroy(state->matrix);
	matrix_global_cleanup();

	memset(state, 0, sizeof(*state));
}

static const char *
tree_string_cb(void *data) {
	return (char *) data;
}

static void
sync_cb(struct matrix *matrix, struct matrix_sync_response *response);

static void *
syncer(void *arg) {
	struct state *state = arg;

	const unsigned sync_timeout = 1000;

	const struct matrix_sync_callbacks callbacks = {
	  .sync_cb = sync_cb,
	  .backoff_cb = NULL,		/* TODO */
	  .backoff_reset_cb = NULL, /* TODO */
	};

	switch (
	  (matrix_sync_forever(state->matrix, NULL, sync_timeout, callbacks))) {
	case MATRIX_NOMEM:
	case MATRIX_CURL_FAILURE:
	default:
		break;
	}

	pthread_exit(NULL);
}

static int
lock_and_push(struct state *state, struct queue_item *item) {
	if (!item) {
		return -1;
	}

	pthread_mutex_lock(&state->mutex);
	queue_push_tail(&state->queue, item);
	pthread_cond_broadcast(&state->cond);
	pthread_mutex_unlock(&state->mutex);

	return 0;
}

static void *
queue_listener(void *arg) {
	struct state *state = arg;

	/* TODO cleanup somehow when loop breaks and items still in queue. */
	while (!state->done) {
		pthread_mutex_lock(&state->mutex);

		struct queue_item *item = NULL;

		while (!(item = queue_pop_head(&state->queue)) && !state->done) {
			/* Sleep until notified and relock mutex. */
			pthread_cond_wait(&state->cond, &state->mutex);
		}

		pthread_mutex_unlock(&state->mutex);

		if (item) {
			queue_item_cb[item->type](state, item->data);
			free(item);
		}
	}

	pthread_exit(NULL);
}

static int
threads_init(struct state *state) {
	if ((pthread_create(&state->threads[THREAD_SYNC], NULL, syncer, state)) == 0
		&& (pthread_create(
			 &state->threads[THREAD_QUEUE], NULL, queue_listener, state))
			 == 0) {
		return 0;
	}

	return -1;
}

static int
ui_init(struct state *state) {
	static char name[] = "Root";

	struct treeview_node *root
	  = treeview_node_alloc(name, tree_string_cb, NULL);

	if ((input_init(&state->input)) == -1
		|| (treeview_init(&state->treeview, root)) == -1) {
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

		char *buf = input_buf(&state->input);

		if ((lock_and_push(state, queue_item_alloc(QUEUE_ITEM_COMMAND, buf)))
			== -1) {
			free(buf);
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

	state->active_widget = INPUT;

	for (;;) {
		switch ((tb_poll_event(&event))) {
		case TB_OK:
			break;
		/* TODO termbox2 bug, resize event is sent after this. */
		case TB_ERR_SELECT:
			continue;
		default:
			return;
		}

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
		ERRLOG(0, "Events for room (%s)\n", room.id);

		struct matrix_state_event sevent;

		while ((matrix_sync_next(&room, &sevent)) == MATRIX_SUCCESS) {
			switch (sevent.type) {
			case MATRIX_ROOM_MEMBER:
				ERRLOG(0, "(%s) => Membership (%s)\n",
				  sevent.member.base.sender, sevent.member.membership);
				break;
			case MATRIX_ROOM_POWER_LEVELS:
				break;
			case MATRIX_ROOM_CANONICAL_ALIAS:
				ERRLOG(0, "Canonical Alias => (%s)\n",
				  sevent.canonical_alias.alias ? sevent.canonical_alias.alias
											   : "");
				break;
			case MATRIX_ROOM_CREATE:
				ERRLOG(0,
				  "Created => Creator (%s), Version (%s), Federate (%d)\n",
				  sevent.create.creator, sevent.create.room_version,
				  sevent.create.federate);
				break;
			case MATRIX_ROOM_JOIN_RULES:
				ERRLOG(0, "Join Rule => (%s)\n", sevent.join_rules.join_rule);
				break;
			case MATRIX_ROOM_NAME:
				ERRLOG(0, "Name => (%s)\n", sevent.name.name);
				break;
			case MATRIX_ROOM_TOPIC:
				ERRLOG(0, "Topic => (%s)\n", sevent.topic.topic);
				break;
			case MATRIX_ROOM_AVATAR:
				ERRLOG(0, "Avatar => (%s)\n", sevent.avatar.url);
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
				ERRLOG(0, "(%s) => (%s)\n", tevent.message.base.sender,
				  tevent.message.body);
				break;
			case MATRIX_ROOM_REDACTION:
				ERRLOG(0, "(%s) redacted by (%s) for (%s)\n",
				  tevent.redaction.redacts, tevent.redaction.base.event_id,
				  tevent.redaction.reason ? tevent.redaction.reason : "");
				break;
			case MATRIX_ROOM_ATTACHMENT:
				ERRLOG(0, "File (%s), URL (%s), Msgtype (%s), Size (%u)\n",
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
							ERRLOG(0, "(%s) => Typing\n", uid);
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

	struct state state
	  = {.cond = PTHREAD_COND_INITIALIZER, .mutex = PTHREAD_MUTEX_INITIALIZER};

	if (!ERRLOG(
		  matrix_global_init() == 0, "Failed to initialize matrix globals.\n")
		&& !ERRLOG(ui_init(&state) == 0, "Failed to initialize UI.\n")
		&& !ERRLOG(state.matrix = matrix_alloc(MXID, HOMESERVER, &state),
		  "Failed to initialize libmatrix.\n")
		&& !ERRLOG(
		  matrix_login(state.matrix, PASS, NULL, NULL) == MATRIX_SUCCESS,
		  "Failed to login.\n")
		&& !ERRLOG(tb_init() == TB_OK, "Failed to initialize termbox.\n")
		&& !ERRLOG(
		  threads_init(&state) == 0, "Failed to initialize threads.\n")) {
		ui_loop(&state);
		cleanup(&state);

		return EXIT_SUCCESS;
	}

	cleanup(&state);
	return EXIT_FAILURE;
}
