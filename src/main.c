/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "cache.h"
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
	_Atomic bool done;
	char *current_room;
	pthread_t threads[THREAD_MAX];
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	struct matrix *matrix;
	struct cache cache;
	struct queue queue;
	struct input input;
	struct treeview treeview;
	struct {
		enum { WIDGET_INPUT = 0, WIDGET_TREE } active_widget;
		enum { TAB_HOME = 0, TAB_CHANNEL } active_tab;
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
	matrix_send_message(state->matrix, &event_id, "!7oDrRRJK2v0eapPf:localhost",
	  "m.text", buf, NULL);
	free(event_id);
}

static struct {
	void (*cb)(struct state *state, void *);
	void (*free)(void *);
} const queue_callbacks[QUEUE_ITEM_MAX] = {
  [QUEUE_ITEM_COMMAND] = {handle_command, free},
};

static struct queue_item *
queue_item_alloc(enum queue_item_type type, void *data) {
	struct queue_item *item
	  = (type < QUEUE_ITEM_MAX && data) ? malloc(sizeof(*item)) : NULL;

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

	enum { bar_height = 1, input_height = 5 };
	struct widget_points points = {0};

	widget_points_set(&points, 0, width, height - input_height, height);
	input_redraw(&state->input, &points);

	widget_points_set(&points, 0, width, bar_height, height);
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

	char *next_batch = cache_next_batch(&state->cache);

	switch (
	  (matrix_sync_forever(state->matrix, NULL, sync_timeout, callbacks))) {
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
		free(item); /* TODO maybe don't free the caller's pointers... */
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

	if ((input_init(&state->input)) == -1
		|| (treeview_init(&state->treeview, root)) == -1) {
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
	assert(state->ui_data.active_widget == WIDGET_INPUT);

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
sync_cb(struct matrix *matrix, struct matrix_sync_response *response) {
	struct state *state = matrix_userp(matrix);

	cache_save(&state->cache, response);
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
