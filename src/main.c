/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "queue.h"
#include "room_ds.h"
#include "stb_ds.h"
#include "widgets.h"

#include <assert.h>
#include <cjson/cJSON.h>
#include <curl/curl.h>
#include <langinfo.h>
#include <locale.h>

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
	_Atomic bool done;
	pthread_t threads[THREAD_MAX];
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	struct matrix *matrix;
	struct cache cache;
	struct queue queue;
	struct {
		enum { WIDGET_INPUT = 0, WIDGET_TREE } active_widget;
		enum { TAB_HOME = 0, TAB_CHANNEL } active_tab;
		struct input input;
		struct treeview treeview;
		struct {
			char *key;
			struct room *value;
		} * rooms;
	} ui_data;
};

struct queue_item {
	enum queue_item_type { QUEUE_ITEM_COMMAND = 0, QUEUE_ITEM_MAX } type;
	void *data;
};

void
handle_command(struct state *state, void *data) {
	char *buf = data;
	assert(buf);

	char *event_id = NULL;
	matrix_send_message(state->matrix, &event_id, "", "m.text", buf, NULL);
	free(event_id);
}

static struct {
	void (*cb)(struct state *, void *);
	void (*free)(void *);
} const queue_callbacks[QUEUE_ITEM_MAX] = {
  [QUEUE_ITEM_COMMAND] = {handle_command, free},
};

static void
queue_item_free(struct queue_item *item) {
	if (item) {
		queue_callbacks[item->type].free(item->data);
		free(item);
	}
}

static struct queue_item *
queue_item_alloc(enum queue_item_type type, void *data) {
	struct queue_item *item
	  = (type < QUEUE_ITEM_MAX && data) ? malloc(sizeof(*item)) : NULL;

	if (item) {
		*item = (struct queue_item) {
		  .type = type,
		  .data = data,
		};
	} else {
		queue_callbacks[type].free(data);
	}

	return item;
}

static void
redraw(struct state *state) {
	tb_clear();

	int height = tb_height();
	int width = tb_width();

	enum { bar_height = 1, input_height = 5 };
	struct widget_points points = {0};

	widget_points_set(&points, 0, width, height - input_height, height);
	input_redraw(&state->ui_data.input, &points);

	widget_points_set(&points, 0, width, bar_height, height);
	treeview_redraw(&state->ui_data.treeview, &points);

	tb_present();
}

static void
cleanup(struct state *state) {
	input_finish(&state->ui_data.input);
	treeview_finish(&state->ui_data.treeview);
	tb_shutdown();

	state->done = true;

	if (state->threads[THREAD_SYNC]) {
		matrix_cancel(state->matrix);
		pthread_join(state->threads[THREAD_SYNC], NULL);
	}

	if (state->threads[THREAD_QUEUE]) {
		pthread_cond_signal(&state->cond);
		pthread_join(state->threads[THREAD_QUEUE], NULL);
	}

	pthread_cond_destroy(&state->cond);
	pthread_mutex_destroy(&state->mutex);

	struct queue_item *item = NULL;

	/* Free any unconsumed items. */
	while ((item = queue_pop_head(&state->queue))) {
		queue_callbacks[item->type].free(item->data);
		free(item);
	}

	cache_finish(&state->cache);

	matrix_destroy(state->matrix);
	matrix_global_cleanup();

	for (size_t i = 0, len = shlenu(state->ui_data.rooms); i < len; i++) {
		room_destroy(state->ui_data.rooms[i].value);
	}
	shfree(state->ui_data.rooms);

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

	const unsigned sync_timeout = 10000;

	const struct matrix_sync_callbacks callbacks = {
	  .sync_cb = sync_cb,
	  .backoff_cb = NULL,		/* TODO */
	  .backoff_reset_cb = NULL, /* TODO */
	};

	char *next_batch = cache_next_batch(&state->cache);

	switch ((matrix_sync_forever(
	  state->matrix, next_batch, sync_timeout, callbacks))) {
	case MATRIX_NOMEM:
	case MATRIX_CURL_FAILURE:
	default:
		break;
	}

	free(next_batch);

	pthread_exit(NULL);
}

static int
lock_and_push(struct state *state, struct queue_item *item) {
	if (!item) {
		return -1;
	}

	pthread_mutex_lock(&state->mutex);
	if ((queue_push_tail(&state->queue, item)) == -1) {
		queue_item_free(item);
		pthread_mutex_unlock(&state->mutex);
		return -1;
	}
	pthread_cond_broadcast(&state->cond);
	/* pthread_cond_wait in queue thread blocks until we unlock the mutex here
	 * before relocking it. */
	pthread_mutex_unlock(&state->mutex);

	return 0;
}

static void *
queue_listener(void *arg) {
	struct state *state = arg;

	while (!state->done) {
		pthread_mutex_lock(&state->mutex);

		struct queue_item *item = NULL;

		while (!(item = queue_pop_head(&state->queue)) && !state->done) {
			pthread_cond_wait(&state->cond, &state->mutex);
		}

		pthread_mutex_unlock(&state->mutex);

		if (item) {
			if (!state->done) {
				queue_callbacks[item->type].cb(state, item->data);
			}
			queue_callbacks[item->type].free(item->data);
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

	if ((input_init(&state->ui_data.input)) == -1
		|| (treeview_init(&state->ui_data.treeview, root)) == -1) {
		treeview_node_destroy(root);
		return -1;
	}

	return 0;
}

static enum widget_error
handle_tree(struct state *state, struct tb_event *event) {
	assert(state->ui_data.active_widget == WIDGET_TREE);
	static char hello[] = "Hello!";

	if (!event->key && event->ch) {
		switch (event->ch) {
		case 'd':
			return treeview_event(&state->ui_data.treeview, TREEVIEW_DELETE);
		case 'h':
			{
				struct treeview_node *node
				  = treeview_node_alloc(hello, tree_string_cb, NULL);

				return treeview_event(
						 &state->ui_data.treeview, TREEVIEW_INSERT_PARENT, node)
						== WIDGET_REDRAW
					   ? WIDGET_REDRAW
					   : (treeview_node_destroy(node), WIDGET_NOOP);
			}
		case 'n':
			{
				struct treeview_node *node
				  = treeview_node_alloc(hello, tree_string_cb, NULL);

				return treeview_event(
						 &state->ui_data.treeview, TREEVIEW_INSERT, node)
						== WIDGET_REDRAW
					   ? WIDGET_REDRAW
					   : (treeview_node_destroy(node), WIDGET_NOOP);
			}
		case ' ':
			return treeview_event(&state->ui_data.treeview, TREEVIEW_EXPAND);
		default:
			break;
		}
	}

	switch (event->key) {
	case TB_KEY_TAB:
		return treeview_event(&state->ui_data.treeview, TREEVIEW_EXPAND);
	case TB_KEY_ARROW_UP:
		return treeview_event(&state->ui_data.treeview, TREEVIEW_UP);
	case TB_KEY_ARROW_DOWN:
		return treeview_event(&state->ui_data.treeview, TREEVIEW_DOWN);
	default:
		break;
	}

	return WIDGET_NOOP;
}

static enum widget_error
handle_input(struct state *state, struct tb_event *event) {
	assert(state->ui_data.active_widget == WIDGET_INPUT);

	if (!event->key && event->ch) {
		return input_handle_event(&state->ui_data.input, INPUT_ADD, event->ch);
	}

	bool mod = (event->mod & TB_MOD_SHIFT);
	bool mod_enter = (event->mod & TB_MOD_ALT);

	switch (event->key) {
	case TB_KEY_ENTER:
		if (mod_enter) {
			return input_handle_event(&state->ui_data.input, INPUT_ADD, '\n');
		}

		char *buf = input_buf(&state->ui_data.input);

		lock_and_push(state, queue_item_alloc(QUEUE_ITEM_COMMAND, buf));
		break;
	case TB_KEY_BACKSPACE:
	case TB_KEY_BACKSPACE2:
		return input_handle_event(
		  &state->ui_data.input, mod ? INPUT_DELETE_WORD : INPUT_DELETE);
	case TB_KEY_ARROW_RIGHT:
		return input_handle_event(
		  &state->ui_data.input, mod ? INPUT_RIGHT_WORD : INPUT_RIGHT);
	case TB_KEY_ARROW_LEFT:
		return input_handle_event(
		  &state->ui_data.input, mod ? INPUT_LEFT_WORD : INPUT_LEFT);
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

	state->ui_data.active_widget = WIDGET_INPUT;

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

		switch (state->ui_data.active_widget) {
		case WIDGET_INPUT:
			ret = handle_input(state, &event);
			break;
		case WIDGET_TREE:
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
populate_from_cache(struct state *state) {
	assert(state);

	struct cache_iterator iterator = {0};
	char *id = NULL;

	if ((cache_rooms_iterator(&state->cache, &iterator, &id)) == 0) {
		while ((cache_iterator_next(&iterator)) == 0) {
			assert(id);
			struct room_info *info = cache_room_info(&state->cache, id);

			if (info) {
				struct room *room = room_alloc(info);

				if (room) {
					shput(state->ui_data.rooms, id, room);
				} else {
					room_info_destroy(info);
				}
			}

			free(id);
			id = NULL; /* Fix false positive in static analyzers, id is
						* reassigned by cache_iterator_next. */
		}

		cache_iterator_finish(&iterator);
	}
}

static int
login(struct state *state) {
	char *access_token = cache_get_token(&state->cache);
	int ret = -1;

	if (access_token) {
		if ((matrix_login_with_token(state->matrix, access_token))
			== MATRIX_SUCCESS) {
			ret = 0;
		}
	} else if ((matrix_login(state->matrix, PASS, NULL, NULL, &access_token))
				 == MATRIX_SUCCESS
			   && (cache_set_token(&state->cache, access_token)) == 0) {
		ret = 0;
	}

	free(access_token);
	return ret;
}

static int
redact(struct room *room, uint64_t index) {
	struct room_index out_index = {0};

	if ((room_bsearch(room, index, &out_index)) == -1) {
		return -1;
	}

	pthread_mutex_lock(&room->nontrivial_modification_mutex);
	struct message *to_redact
	  = &room->timelines[out_index.index_timeline][out_index.index_buf]
		   .buf[out_index.index_message];
	to_redact->redacted = true;
	free(to_redact->body);
	to_redact->body = NULL;
	pthread_mutex_unlock(&room->nontrivial_modification_mutex);

	return 0;
}

static void
sync_cb(struct matrix *matrix, struct matrix_sync_response *response) {
	struct state *state = matrix_userp(matrix);

	assert(state);

	struct matrix_room sync_room;
	struct cache_save_txn txn = {0};

	if ((cache_save_txn_init(&state->cache, &txn)) != 0) {
		return;
	}

	cache_save_next_batch(&txn, response->next_batch);

	while ((matrix_sync_room_next(response, &sync_room)) == 0) {
		switch (sync_room.type) {
		case MATRIX_ROOM_LEAVE:
			break;
		case MATRIX_ROOM_JOIN:
			break;
		case MATRIX_ROOM_INVITE:
			break;
		default:
			assert(0);
		}

		if ((cache_set_room_dbs(&txn, &sync_room)) != 0
			|| (cache_save_room(&txn, &sync_room)) != 0) {
			continue;
		}

		struct matrix_sync_event event;

		struct room *room = shget(state->ui_data.rooms, sync_room.id);
		bool room_needs_info = !room;

		if (room_needs_info) {
			room = room_alloc(NULL);

			if (!room) {
				continue;
			}
		}

		struct timeline *timeline = room->timelines[TIMELINE_FORWARD];
		size_t len = timeline->len;

		while ((matrix_sync_event_next(&sync_room, &event)) == 0) {
			uint64_t index = txn.index;
			enum cache_save_error ret = CACHE_FAIL;

			if ((ret = cache_save_event(&txn, &event)) == CACHE_FAIL) {
				continue;
			}

			/* Declare variables here to avoid adding a new scope in the switch
			 * as it increases the indentation level needlessly. */
			struct matrix_timeline_event *tevent = NULL;

			switch (event.type) {
			case MATRIX_EVENT_EPHEMERAL:
				break;
			case MATRIX_EVENT_STATE:
				break;
			case MATRIX_EVENT_TIMELINE:
				tevent = &event.timeline;

				switch (tevent->type) {
				case MATRIX_ROOM_MESSAGE:
					if ((len + 1) > TIMELINE_BUFSZ) {
						assert(0);
						break; /* TODO */
					}

					assert(tevent->base.sender);
					assert(tevent->message.body);

					/* This is safe as the reader thread will have the old
					 * value of len stored and not access anything beyond
					 * that. */
					timeline->buf[len++] = (struct message) {
					  .formatted = false,
					  .reply = false,
					  .index = index,
					  .body = strdup(tevent->message.body),
					  .sender = strdup(tevent->base.sender),
					};
					break;
				case MATRIX_ROOM_REDACTION:
					if (ret == CACHE_GOT_REDACTION) {
						timeline->len = len; /* Ensure that bsearch has
												access to new events. */

						redact(room, txn.latest_redaction);
					}
					break;
				case MATRIX_ROOM_ATTACHMENT:
					break;
				default:
					assert(0);
				}
				break;
			default:
				assert(0);
			}
		}

		timeline->len = len;

		if (room_needs_info) {
			if ((room->info = cache_room_info(&state->cache, sync_room.id))) {
				shput(state->ui_data.rooms, sync_room.id, room);
			} else {
				room_destroy(room);
			}
		}
	}

	cache_save_txn_finish(&txn);
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
		&& !ERRLOG(
		  cache_init(&state.cache) == 0, "Failed to initialize database.\n")
		&& !ERRLOG(ui_init(&state) == 0, "Failed to initialize UI.\n")
		&& !ERRLOG(state.matrix = matrix_alloc(MXID, HOMESERVER, &state),
		  "Failed to initialize libmatrix.\n")
		&& !ERRLOG(login(&state) == 0, "Failed to login.\n")
		&& !ERRLOG(tb_init() == TB_OK, "Failed to initialize termbox.\n")
		&& !ERRLOG(
		  threads_init(&state) == 0, "Failed to initialize threads.\n")) {
		sh_new_strdup(
		  state.ui_data.rooms); /* Important to avoid use after frees. */
		populate_from_cache(&state);

		ui_loop(&state); /* Blocks forever. */
		cleanup(&state);

		return EXIT_SUCCESS;
	}

	cleanup(&state);
	return EXIT_FAILURE;
}
