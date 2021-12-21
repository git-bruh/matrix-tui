/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "login_form.h"
#include "queue.h"
#include "room_ds.h"

#include <assert.h>
#include <langinfo.h>
#include <locale.h>

#define ERRLOG(cond, ...)                                                      \
	(!(cond) ? (fprintf(stderr, __VA_ARGS__), true) : false)

enum { THREAD_SYNC = 0, THREAD_QUEUE, THREAD_MAX };
enum { PIPE_READ = 0, PIPE_WRITE, PIPE_MAX };
enum { FD_TTY = 0, FD_RESIZE, FD_PIPE, FD_MAX };

struct state {
	_Atomic bool done;
	pthread_t threads[THREAD_MAX];
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	struct matrix *matrix;
	struct cache cache;
	struct queue queue;
	struct {
		enum { WIDGET_INPUT = 0, WIDGET_TREE, WIDGET_FORM } active_widget;
		enum { TAB_HOME = 0, TAB_LOGIN, TAB_CHANNEL } active_tab;
		int thread_comm_pipe[PIPE_MAX];
		struct input input;
		struct treeview treeview;
		struct form form;
		struct {
			char *key;
			struct room *value;
		} * rooms;
		const char *error;
		struct room *current_room;
		pthread_mutex_t rooms_mutex;
	} ui_data;
};

struct queue_item {
	enum queue_item_type {
		QUEUE_ITEM_COMMAND = 0,
		QUEUE_ITEM_LOGIN,
		QUEUE_ITEM_MAX
	} type;
	void *data;
};

static void
sync_cb(struct matrix *matrix, struct matrix_sync_response *response);

void
handle_command(struct state *state, void *data) {
	char *buf = data;
	assert(buf);

	char *event_id = NULL;
	matrix_send_message(state->matrix, &event_id, "", "m.text", buf, NULL);
	free(event_id);
}

void
handle_login(struct state *state, void *data) {
	assert(state);
	assert(data);

	char *password = data;
	char *access_token = NULL;

	enum matrix_code code
	  = matrix_login(state->matrix, password, NULL, NULL, &access_token);

	if (code == MATRIX_SUCCESS) {
		assert(access_token);

		char *mxid = NULL;
		char *homeserver = NULL;

		matrix_get_mxid_homeserver(state->matrix, &mxid, &homeserver);

		assert(mxid);
		assert(homeserver);

		int ret = -1;

		ret = cache_auth_set(&state->cache, DB_KEY_ACCESS_TOKEN, access_token);
		assert(ret == 0);
		ret = cache_auth_set(&state->cache, DB_KEY_MXID, mxid);
		assert(ret == 0);
		ret = cache_auth_set(&state->cache, DB_KEY_HOMESERVER, homeserver);
		assert(ret == 0);
	}

	(void) write(
	  state->ui_data.thread_comm_pipe[PIPE_WRITE], &code, sizeof(code));

	free(access_token);
}

static struct {
	void (*cb)(struct state *, void *);
	void (*free)(void *);
} const queue_callbacks[QUEUE_ITEM_MAX] = {
  [QUEUE_ITEM_COMMAND] = {handle_command, free},
  [QUEUE_ITEM_LOGIN] = {	handle_login, free},
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

	enum {
		bar_height = 1,
		input_height = 5,
		form_width = 68,
		form_art_gap = 2,
	};

	struct widget_points points = {0};

	if (state->ui_data.active_tab == TAB_LOGIN) {
		static const char *art[]
		  = {"███╗███╗   ███╗ █████╗ ████████╗██████╗ ██╗██╗  ██╗███╗",
			"██╔╝████╗ ████║██╔══██╗╚══██╔══╝██╔══██╗██║╚██╗██╔╝╚██║",
			"██║ ██╔████╔██║███████║   ██║   ██████╔╝██║ ╚███╔╝  ██║",
			"██║ ██║╚██╔╝██║██╔══██║   ██║   ██╔══██╗██║ ██╔██╗  ██║",
			"███╗██║ ╚═╝ ██║██║  ██║   ██║   ██║  ██║██║██╔╝ ██╗███║",
			"╚══╝╚═╝     ╚═╝╚═╝  ╚═╝   ╚═╝   ╚═╝  ╚═╝╚═╝╚═╝  ╚═╝╚══╝"};

		assert(state->ui_data.active_widget == WIDGET_FORM);

		int padding_x = widget_pad_center(form_width, width);
		int padding_y = widget_pad_center(FORM_HEIGHT, height - bar_height);

		int padding_art = widget_pad_center(widget_str_width(*art), width);
		int art_y = (bar_height + padding_y) - form_art_gap;

		for (size_t i = (sizeof(art) / sizeof(*art));
			 i > 0 && art_y >= bar_height; i--, art_y--) {
			widget_print_str(
			  padding_art, art_y, width, TB_DEFAULT, TB_DEFAULT, art[i - 1]);
		}

		widget_points_set(&points, padding_x, width - padding_x,
		  bar_height + padding_y, height - padding_y);
		form_redraw(&state->ui_data.form, &points);

		if (state->ui_data.error) {
			widget_print_str(
			  widget_pad_center(widget_str_width(state->ui_data.error), width),
			  (height - padding_y) + 1, width, TB_RED, TB_DEFAULT,
			  state->ui_data.error);
		}

		tb_present();
		return;
	}

	widget_points_set(&points, 0, width, height - input_height, height);

	int input_rows = 0;
	input_redraw(&state->ui_data.input, &points, &input_rows);

	widget_points_set(&points, 0, width, bar_height, height);
	treeview_redraw(&state->ui_data.treeview, &points);

	pthread_mutex_lock(&state->ui_data.rooms_mutex);
	if (!state->ui_data.current_room && (shlenu(state->ui_data.rooms)) > 0) {
		state->ui_data.current_room = state->ui_data.rooms[0].value;
	}

	struct room *room = state->ui_data.current_room;
	pthread_mutex_unlock(&state->ui_data.rooms_mutex);

	if (room) {
		pthread_mutex_lock(&room->realloc_or_modify_mutex);

		widget_points_set(&points, 0, width, bar_height, height - input_rows);

		struct message **buf = room->timelines[TIMELINE_FORWARD].buf;
		size_t len = room->timelines[TIMELINE_FORWARD].len;

		if ((message_buffer_should_recalculate(&room->buffer, &points))) {
			message_buffer_zero(&room->buffer);

			for (size_t i = 0; i < len; i++) {
				if (!buf[i]->redacted) {
					message_buffer_insert(&room->buffer, &points, buf[i]);
				}
			}

			message_buffer_ensure_sane_scroll(&room->buffer);
		}

		message_buffer_redraw(&room->buffer, &points);

		pthread_mutex_unlock(&room->realloc_or_modify_mutex);
	}

	tb_present();
}

static void
cleanup(struct state *state) {
	input_finish(&state->ui_data.input);
	treeview_finish(&state->ui_data.treeview);
	tb_shutdown();

	state->done = true;
	/* Cancel here as we might be logging in, so the syncer thread would
	 * not have started. */
	matrix_cancel(state->matrix);

	if (state->threads[THREAD_SYNC]) {
		pthread_join(state->threads[THREAD_SYNC], NULL);
	}

	if (state->threads[THREAD_QUEUE]) {
		pthread_cond_signal(&state->cond);
		pthread_join(state->threads[THREAD_QUEUE], NULL);
	}

	for (size_t i = 0; i < PIPE_MAX; i++) {
		if (state->ui_data.thread_comm_pipe[i] != -1) {
			close(state->ui_data.thread_comm_pipe[i]);
		}
	}

	pthread_cond_destroy(&state->cond);
	pthread_mutex_destroy(&state->mutex);

	struct queue_item *item = NULL;

	/* Free any unconsumed items. */
	while ((item = queue_pop_head(&state->queue))) {
		queue_item_free(item);
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

static void *
syncer(void *arg) {
	struct state *state = arg;

	const unsigned sync_timeout = 10000;

	const struct matrix_sync_callbacks callbacks = {
	  .sync_cb = sync_cb,
	  .backoff_cb = NULL,		/* TODO */
	  .backoff_reset_cb = NULL, /* TODO */
	};

	char *next_batch = cache_auth_get(&state->cache, DB_KEY_NEXT_BATCH);

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
ui_init(struct state *state) {
	static char name[] = "Root";

	struct treeview_node *root
	  = treeview_node_alloc(name, tree_string_cb, NULL);

	if ((input_init(&state->ui_data.input, TB_DEFAULT, false)) == -1
		|| (treeview_init(&state->ui_data.treeview, root)) == -1
		|| (form_init(&state->ui_data.form, TB_BLUE)) == -1) {
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

static enum widget_error
handle_message_buffer(struct state *state, struct tb_event *event) {
	assert(event->type == TB_EVENT_MOUSE);

	switch (event->key) {
	case TB_KEY_MOUSE_WHEEL_UP:
		if ((message_buffer_handle_event(
			  &state->ui_data.current_room->buffer, MESSAGE_BUFFER_UP))
			== WIDGET_NOOP) {
			/* TODO paginate(); */
		} else {
			return WIDGET_REDRAW;
		}
		break;
	case TB_KEY_MOUSE_WHEEL_DOWN:
		return message_buffer_handle_event(
		  &state->ui_data.current_room->buffer, MESSAGE_BUFFER_DOWN);
	default:
		break;
	}

	return WIDGET_NOOP;
}

static int
get_fds(struct state *state, struct pollfd fds[FD_MAX]) {
	assert(state);
	assert(fds);

	int ttyfd = -1;
	int resizefd = -1;

	if ((tb_get_fds(&ttyfd, &resizefd)) != TB_OK) {
		assert(0);
		return -1;
	}

	assert(ttyfd != -1);
	assert(resizefd != -1);

	fds[FD_TTY] = (struct pollfd) {.fd = ttyfd, .events = POLLIN};
	fds[FD_RESIZE] = (struct pollfd) {.fd = resizefd, .events = POLLIN};
	fds[FD_PIPE] = (struct pollfd) {
	  .fd = state->ui_data.thread_comm_pipe[PIPE_READ], .events = POLLIN};

	return 0;
}

static void
ui_loop(struct state *state) {
	tb_set_input_mode(TB_INPUT_ALT | TB_INPUT_MOUSE);
	tb_set_output_mode(TB_OUTPUT_256);

	state->ui_data.active_tab = TAB_HOME;
	state->ui_data.active_widget = WIDGET_INPUT;

	redraw(state);

	struct tb_event event = {0};

	struct pollfd fds[FD_MAX] = {0};
	get_fds(state, fds);

	for (;;) {
		int fds_with_data = poll(fds, FD_MAX, -1);

		if (fds_with_data > 0 && (fds[FD_PIPE].revents & POLLIN)) {
			fds_with_data--;

			uintptr_t room = 0;

			if ((read(state->ui_data.thread_comm_pipe[PIPE_READ], &room,
				  sizeof(room)))
				  == sizeof(room)
				&& room == ((uintptr_t) state->ui_data.current_room)) {
				redraw(state);
			}
		}

		if (fds_with_data <= 0) {
			continue;
		}

		if ((tb_poll_event(&event)) != TB_OK) {
			continue;
		}

		enum widget_error ret = WIDGET_NOOP;

		if (event.type == TB_EVENT_RESIZE) {
			redraw(state);
			continue;
		}

		if (event.key == TB_KEY_CTRL_C) {
			return;
		}

		if (event.type == TB_EVENT_MOUSE) {
			ret = handle_message_buffer(state, &event);
		} else {
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
		}

		if (ret == WIDGET_REDRAW) {
			redraw(state);
		}
	}
}

static int
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

	return 0;
}

static int
login_with_info(struct state *state) {
	assert(state);

	int ret = -1;

	char *username = input_buf(&state->ui_data.form.fields[FIELD_MXID]);
	char *password = input_buf(&state->ui_data.form.fields[FIELD_PASSWORD]);
	char *homeserver = input_buf(&state->ui_data.form.fields[FIELD_HOMESERVER]);

	bool password_sent_to_queue = false;

	if (username && password && homeserver) {
		if (state->matrix) {
			if ((matrix_set_mxid_homeserver(
				  state->matrix, username, homeserver))
				== 0) {
				ret = 0;
			}
		} else {
			ret = (state->matrix = matrix_alloc(username, homeserver, state))
				  ? 0
				  : -1;
		}

		if (ret == 0) {
			if (lock_and_push(
				  state, queue_item_alloc(QUEUE_ITEM_LOGIN, password))
				== 0) {
				password_sent_to_queue = true;
				ret = 0;
			} else {
				password = NULL; /* Freed */
			}
		}
	}

	free(username);
	free(homeserver);

	if (!password_sent_to_queue) {
		free(password);
	}

	return ret;
}

static int
login(struct state *state) {
	assert(state);

	char *access_token = cache_auth_get(&state->cache, DB_KEY_ACCESS_TOKEN);
	char *mxid = cache_auth_get(&state->cache, DB_KEY_MXID);
	char *homeserver = cache_auth_get(&state->cache, DB_KEY_HOMESERVER);

	int ret = -1;

	if (access_token && mxid && homeserver
		&& (state->matrix = matrix_alloc(mxid, homeserver, state))
		&& (matrix_login_with_token(state->matrix, access_token))
			 == MATRIX_SUCCESS) {
		ret = 0;
	}

	free(access_token);
	free(mxid);
	free(homeserver);

	if (ret == 0) {
		return ret;
	}

	state->ui_data.active_tab = TAB_LOGIN;
	state->ui_data.active_widget = WIDGET_FORM;

	redraw(state);

	struct tb_event event = {0};

	struct pollfd fds[FD_MAX] = {0};
	get_fds(state, fds);

	bool logging_in = false;

	for (;;) {
		int fds_with_data = poll(fds, FD_MAX, -1);

		if (fds_with_data > 0 && (fds[FD_PIPE].revents & POLLIN)) {
			fds_with_data--;

			enum matrix_code code = MATRIX_SUCCESS;

			if ((read(state->ui_data.thread_comm_pipe[PIPE_READ], &code,
				  sizeof(code)))
				== sizeof(code)) {
				logging_in = false;

				if (code == MATRIX_SUCCESS) {
					state->ui_data.error = NULL;
					redraw(state);

					ret = 0;
					break;
				} else {
					state->ui_data.error = matrix_strerror(code);
					redraw(state);
				}
			}
		}

		if (fds_with_data <= 0 || (tb_poll_event(&event)) != TB_OK) {
			continue;
		}

		if (event.type == TB_EVENT_RESIZE) {
			redraw(state);
			continue;
		}

		if (event.key == TB_KEY_CTRL_C) {
			ret = -1; /* Transfer cancelled and thread killed in cleanup() */
			break;
		}

		if (logging_in) {
			continue;
		}

		enum widget_error should_redraw = WIDGET_NOOP;

		switch (event.key) {
		case TB_KEY_ARROW_UP:
			should_redraw = form_handle_event(&state->ui_data.form, FORM_UP);
			break;
		case TB_KEY_ARROW_DOWN:
			should_redraw = form_handle_event(&state->ui_data.form, FORM_DOWN);
			break;
		case TB_KEY_ENTER:
			if (!state->ui_data.form.button_is_selected) {
				break;
			}

			if ((login_with_info(state)) == 0) {
				state->ui_data.error = NULL;
				logging_in = true;
			} else {
				state->ui_data.error = "Invalid Information";
			}

			should_redraw = WIDGET_REDRAW;
			break;
		default:
			{
				struct input *input = form_current_input(&state->ui_data.form);

				if (!input) {
					break;
				}

				if (!event.key && event.ch) {
					should_redraw
					  = input_handle_event(input, INPUT_ADD, event.ch);
				} else {
					switch (event.key) {
					case TB_KEY_BACKSPACE:
					case TB_KEY_BACKSPACE2:
						should_redraw = input_handle_event(input, INPUT_DELETE);
						break;
					case TB_KEY_ARROW_RIGHT:
						should_redraw = input_handle_event(input, INPUT_RIGHT);
						break;
					case TB_KEY_ARROW_LEFT:
						should_redraw = input_handle_event(input, INPUT_LEFT);
						break;
					}
				}
			}
			break;
		}

		if (should_redraw == WIDGET_REDRAW) {
			redraw(state);
		}
	}

	form_finish(&state->ui_data.form);
	return ret;
}

static int
redact(struct room *room, uint64_t index) {
	assert(room);

	struct room_index out_index = {0};

	if ((room_bsearch(room, index, &out_index)) == -1) {
		return -1;
	}

	pthread_mutex_lock(&room->realloc_or_modify_mutex);
	struct message *to_redact
	  = room->timelines[out_index.index_timeline].buf[out_index.index_buf];
	to_redact->redacted = true;
	free(to_redact->body);
	to_redact->body = NULL;
	message_buffer_redact(&room->buffer, index);
	pthread_mutex_unlock(&room->realloc_or_modify_mutex);

	return 0;
}

static void
sync_cb(struct matrix *matrix, struct matrix_sync_response *response) {
	struct state *state = matrix_userp(matrix);

	assert(state);

	struct matrix_room sync_room;
	struct cache_save_txn txn = {0};

	if ((cache_auth_set(&state->cache, DB_KEY_NEXT_BATCH, response->next_batch))
		  != 0
		|| (cache_save_txn_init(&state->cache, &txn)) != 0) {
		return;
	}

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

		pthread_mutex_lock(&state->ui_data.rooms_mutex);
		struct room *room = shget(state->ui_data.rooms, sync_room.id);
		pthread_mutex_unlock(&state->ui_data.rooms_mutex);

		bool room_needs_info = !room;

		if (room_needs_info) {
			room = room_alloc(NULL);

			if (!room) {
				continue;
			}
		}

		struct timeline *timeline = &room->timelines[TIMELINE_FORWARD];

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
					lock_if_grow(timeline->buf, &room->realloc_or_modify_mutex);

					assert(tevent->base.sender);
					assert(tevent->message.body);

					/* This is safe as the reader thread will have the old value
					 * of len stored and not access anything beyond that. */
					struct message *message
					  = message_alloc(tevent->message.body, tevent->base.sender,
						index, NULL, false);

					if (!message) {
						break;
					}

					arrput(timeline->buf, message);
					break;
				case MATRIX_ROOM_REDACTION:
					if (ret == CACHE_GOT_REDACTION) {
						/* Ensure that bsearch has access to new events. */
						timeline->len = arrlenu(timeline->buf);

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

		timeline->len = arrlenu(timeline->buf);

		if (room_needs_info) {
			if ((room->info = cache_room_info(&state->cache, sync_room.id))) {
				pthread_mutex_lock(&state->ui_data.rooms_mutex);
				shput(state->ui_data.rooms, sync_room.id, room);
				pthread_mutex_unlock(&state->ui_data.rooms_mutex);

				uintptr_t ptr = (uintptr_t) NULL;
				(void) write(state->ui_data.thread_comm_pipe[PIPE_WRITE], &ptr,
				  sizeof(ptr));
			} else {
				room_destroy(room);
			}
		} else {
			uintptr_t ptr = (uintptr_t) room;
			(void) write(
			  state->ui_data.thread_comm_pipe[PIPE_WRITE], &ptr, sizeof(ptr));
		}
	}

	cache_save_txn_finish(&txn);
}

int
hm_init(struct state *state) {
	/* sh_new_strdup is important to avoid use after frees. Must call these
	 * 2 functions here before the syncer thread starts. */
	sh_new_strdup(state->ui_data.rooms);
	populate_from_cache(state);

	return 0;
}

int
main() {
	if (ERRLOG(setlocale(LC_ALL, ""), "Failed to set locale.\n")
		|| ERRLOG(strcmp("UTF-8", nl_langinfo(CODESET)) == 0,
		  "Locale is not UTF-8.\n")) {
		return EXIT_FAILURE;
	}

	struct state state = {
	  .cond = PTHREAD_COND_INITIALIZER,
	  .mutex = PTHREAD_MUTEX_INITIALIZER,
	  .ui_data = {.rooms_mutex = PTHREAD_MUTEX_INITIALIZER,
					.thread_comm_pipe = {-1, -1}}
	 };

	if (!ERRLOG(
		  matrix_global_init() == 0, "Failed to initialize matrix globals.\n")
		&& !ERRLOG(pipe(state.ui_data.thread_comm_pipe) == 0,
		  "Failed to initialize pipe.\n")
		&& !ERRLOG(
		  cache_init(&state.cache) == 0, "Failed to initialize database.\n")
		&& !ERRLOG(ui_init(&state) == 0, "Failed to initialize UI.\n")
		&& !ERRLOG(tb_init() == TB_OK, "Failed to initialize termbox.\n")
		&& !ERRLOG(pthread_create(
					 &state.threads[THREAD_QUEUE], NULL, queue_listener, &state)
					 == 0,
		  "Failed to initialize queue thread.\n")
		&& (login(&state)) == 0
		&& hm_init(&state) == 0
		/* TODO die and log function */
		&& (pthread_create(&state.threads[THREAD_SYNC], NULL, syncer, &state))
			 == 0) {
		ui_loop(&state); /* Blocks forever. */
		cleanup(&state);

		return EXIT_SUCCESS;
	}

	cleanup(&state);
	return EXIT_FAILURE;
}
