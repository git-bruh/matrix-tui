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

enum widget {
	WIDGET_INPUT = 0,
	WIDGET_FORM,
	WIDGET_TREE,
};

enum tab { TAB_LOGIN = 0, TAB_HOME, TAB_ROOM };

struct state {
	_Atomic bool done;
	pthread_t threads[THREAD_MAX];
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	struct matrix *matrix;
	struct cache cache;
	struct queue queue;
	/* Pass data between the syncer thread and the UI thread. This exists as the
	 * UI thread can't block forever, listening to a queue as it has to handle
	 * input and resizes. Instead, we poll on this pipe along with polling for
	 * events from the terminal. */
	int thread_comm_pipe[PIPE_MAX];
	struct {
		char *key;
		struct room *value;
	} * rooms;
	pthread_mutex_t rooms_mutex;
};

struct queue_item {
	enum queue_item_type {
		QUEUE_ITEM_COMMAND = 0,
		QUEUE_ITEM_LOGIN,
		QUEUE_ITEM_MAX
	} type;
	void *data;
};

enum read_or_write { READ = 0, WRITE };

/* Wrapper for read() / write() immune to EINTR. */
static ssize_t
safe_read_or_write(
  int fildes, void *buf, size_t nbyte, enum read_or_write what) {
	ssize_t ret = 0;

	while (nbyte > 0) {
		do {
			ret = (what == READ) ? (read(fildes, buf, nbyte))
								 : (write(fildes, buf, nbyte));
		} while ((ret < 0) && (errno == EINTR || errno == EAGAIN));

		if (ret < 0) {
			return ret;
		}

		nbyte -= (size_t) ret;
		/* Increment buffer */
		buf = &((unsigned char *) buf)[ret];
	}

	return 0;
}

#define read(fildes, buf, byte) safe_read_or_write(fildes, buf, byte, READ)
#define write(fildes, buf, nbyte) safe_read_or_write(fildes, buf, nbyte, WRITE)

static void
handle_command(struct state *state, void *data) {
	char *buf = data;
	assert(buf);

	char *event_id = NULL;
	matrix_send_message(state->matrix, &event_id, "", "m.text", buf, NULL);
	free(event_id);
}

static void
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

	write(state->thread_comm_pipe[PIPE_WRITE], &code, sizeof(code));

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
cleanup(struct state *state) {
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
		if (state->thread_comm_pipe[i] != -1) {
			close(state->thread_comm_pipe[i]);
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

	for (size_t i = 0, len = shlenu(state->rooms); i < len; i++) {
		room_destroy(state->rooms[i].value);
	}
	shfree(state->rooms);

	memset(state, 0, sizeof(*state));
}

static void
sync_cb(struct matrix *matrix, struct matrix_sync_response *response);

static void *
syncer(void *arg) {
	assert(arg);

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
ui_init(void) {
	if ((tb_init()) == TB_OK) {
		tb_set_input_mode(TB_INPUT_ALT | TB_INPUT_MOUSE);
		/* TODO tb_set_output_mode(TB_OUTPUT_256); */

		return 0;
	}

	return -1;
}

static enum widget_error
handle_input(struct input *input, struct tb_event *event, bool *enter_pressed) {
	assert(input);
	assert(event);

	if (!event->key && event->ch) {
		return input_handle_event(input, INPUT_ADD, event->ch);
	}

	bool mod = (event->mod & TB_MOD_SHIFT);
	bool mod_enter = (event->mod & TB_MOD_ALT);

	switch (event->key) {
	case TB_KEY_ENTER:
		if (mod_enter) {
			return input_handle_event(input, INPUT_ADD, '\n');
		}

		if (enter_pressed) {
			*enter_pressed = true;
		}
		break;
	case TB_KEY_BACKSPACE:
	case TB_KEY_BACKSPACE2:
		return input_handle_event(
		  input, mod ? INPUT_DELETE_WORD : INPUT_DELETE);
	case TB_KEY_ARROW_RIGHT:
		return input_handle_event(input, mod ? INPUT_RIGHT_WORD : INPUT_RIGHT);
	case TB_KEY_ARROW_LEFT:
		return input_handle_event(input, mod ? INPUT_LEFT_WORD : INPUT_LEFT);
	default:
		break;
	}

	return WIDGET_NOOP;
}

static enum widget_error
handle_message_buffer(struct message_buffer *buf, struct tb_event *event) {
	assert(event->type == TB_EVENT_MOUSE);

	switch (event->key) {
	case TB_KEY_MOUSE_WHEEL_UP:
		if ((message_buffer_handle_event(buf, MESSAGE_BUFFER_UP))
			== WIDGET_NOOP) {
			/* TODO paginate(); */
		} else {
			return WIDGET_REDRAW;
		}
		break;
	case TB_KEY_MOUSE_WHEEL_DOWN:
		return message_buffer_handle_event(buf, MESSAGE_BUFFER_DOWN);
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
	  .fd = state->thread_comm_pipe[PIPE_READ], .events = POLLIN};

	return 0;
}

static struct room *
hm_first_room(struct state *state) {
	assert(state);

	struct room *room = NULL;

	pthread_mutex_lock(&state->rooms_mutex);

	if ((shlenu(state->rooms)) > 0) {
		room = state->rooms[0].value;
	}

	pthread_mutex_unlock(&state->rooms_mutex);

	return room;
}

static void
reset_message_buffer_if_recalculate(struct room *room) {
	assert(room);

	void tab_room_get_buffer_points(struct widget_points * points);

	struct widget_points points = {0};
	tab_room_get_buffer_points(&points);

	pthread_mutex_lock(&room->realloc_or_modify_mutex);

	struct message **buf = room->timelines[TIMELINE_FORWARD].buf;
	size_t len = room->timelines[TIMELINE_FORWARD].len;

	message_buffer_zero(&room->buffer);

	for (size_t i = 0; i < len; i++) {
		if (!buf[i]->redacted) {
			message_buffer_insert(&room->buffer, &points, buf[i]);
		}
	}

	message_buffer_ensure_sane_scroll(&room->buffer);

	pthread_mutex_unlock(&room->realloc_or_modify_mutex);
}

static size_t
fill_new_events(struct room *room, size_t already_consumed) {
	assert(room);

	struct widget_points points = {0};
	tab_room_get_buffer_points(&points);

	pthread_mutex_lock(&room->realloc_or_modify_mutex);

	struct message **buf = room->timelines[TIMELINE_FORWARD].buf;
	size_t len = room->timelines[TIMELINE_FORWARD].len;

	for (; already_consumed < len; already_consumed++) {
		if (!buf[already_consumed]->redacted) {
			message_buffer_insert(
			  &room->buffer, &points, buf[already_consumed]);
		}
	}

	pthread_mutex_unlock(&room->realloc_or_modify_mutex);

	return already_consumed;
}

static void
ui_loop(struct state *state) {
	assert(state);
	void tab_room_redraw(struct input * input, struct room * room);

	enum widget widget = WIDGET_INPUT;
	enum tab tab = TAB_ROOM;

	struct tb_event event = {0};
	struct pollfd fds[FD_MAX] = {0};

	get_fds(state, fds);

	struct input input = {0};

	if ((input_init(&input, TB_DEFAULT, false)) != 0) {
		return;
	}

	struct room *room = hm_first_room(state);
	size_t already_consumed = 0;

	for (bool redraw = true;;) {
		if (redraw) {
			redraw = false;

			tb_clear();

			switch (tab) {
			case TAB_HOME:
				break;
			case TAB_ROOM:
				if (room) {
					tab_room_redraw(&input, room);
				}
				break;
			default:
				assert(0);
			}

			tb_present();
		}

		int fds_with_data = poll(fds, FD_MAX, -1);

		if (fds_with_data > 0 && (fds[FD_PIPE].revents & POLLIN)) {
			fds_with_data--;

			uintptr_t read_room = 0;

			if ((read(state->thread_comm_pipe[PIPE_READ], &read_room,
				  sizeof(read_room)))
				  == 0
				&& read_room == (uintptr_t) room) {
				if (!room) {
					room = hm_first_room(state);
				}

				already_consumed = fill_new_events(room, already_consumed);
				redraw = true;
			}
		}

		if (fds_with_data <= 0 || (tb_poll_event(&event)) != TB_OK) {
			continue;
		}

		enum widget_error ret = WIDGET_NOOP;

		if (event.type == TB_EVENT_RESIZE) {
			if (room) {
				reset_message_buffer_if_recalculate(room);
			}

			redraw = true;
			continue;
		}

		if (event.key == TB_KEY_CTRL_C) {
			break;
		}

		switch (tab) {
		case TAB_HOME:
			break;
		case TAB_ROOM:
			if (event.type == TB_EVENT_MOUSE) {
				if (room) {
					pthread_mutex_lock(&room->realloc_or_modify_mutex);
					ret = handle_message_buffer(&room->buffer, &event);
					pthread_mutex_unlock(&room->realloc_or_modify_mutex);
				}
			} else {
				switch (widget) {
				case WIDGET_INPUT:
					ret = handle_input(&input, &event, NULL);
					break;
				default:
					break;
				}
			}
			break;
		default:
			assert(0);
		}

		if (ret == WIDGET_REDRAW) {
			redraw = true;
		}
	}

	input_finish(&input);
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
					shput(state->rooms, id, room);
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
login_with_info(struct state *state, struct form *form) {
	assert(state);

	int ret = -1;

	char *username = input_buf(&form->fields[FIELD_MXID]);
	char *password = input_buf(&form->fields[FIELD_PASSWORD]);
	char *homeserver = input_buf(&form->fields[FIELD_HOMESERVER]);

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
	void tab_login_redraw(struct form * form, const char *error);

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

	struct form form = {0};

	if ((form_init(&form, TB_BLUE)) == -1) {
		return ret;
	}

	struct tb_event event = {0};

	struct pollfd fds[FD_MAX] = {0};
	get_fds(state, fds);

	bool logging_in = false;
	const char *error = NULL;

	for (bool redraw = true;;) {
		if (redraw) {
			redraw = false;

			tb_clear();
			tab_login_redraw(&form, error);
			tb_present();
		}

		int fds_with_data = poll(fds, FD_MAX, -1);

		if (fds_with_data > 0 && (fds[FD_PIPE].revents & POLLIN)) {
			fds_with_data--;

			enum matrix_code code = MATRIX_SUCCESS;

			if ((read(state->thread_comm_pipe[PIPE_READ], &code, sizeof(code)))
				== 0) {
				logging_in = false;

				if (code == MATRIX_SUCCESS) {
					error = NULL;
					ret = 0;
				} else {
					error = matrix_strerror(code);
				}

				tb_clear();
				tab_login_redraw(&form, error);
				tb_present();

				if (ret == 0) {
					break;
				}
			}
		}

		if (fds_with_data <= 0 || (tb_poll_event(&event)) != TB_OK) {
			continue;
		}

		if (event.type == TB_EVENT_RESIZE) {
			redraw = true;
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
			should_redraw = form_handle_event(&form, FORM_UP);
			break;
		case TB_KEY_ARROW_DOWN:
			should_redraw = form_handle_event(&form, FORM_DOWN);
			break;
		case TB_KEY_ENTER:
			if (!form.button_is_selected) {
				break;
			}

			if ((login_with_info(state, &form)) == 0) {
				error = NULL;
				logging_in = true;
			} else {
				error = "Invalid Information";
			}

			should_redraw = WIDGET_REDRAW;
			break;
		default:
			{
				struct input *input = form_current_input(&form);

				if (input) {
					should_redraw = handle_input(input, &event, NULL);
				}
			}
			break;
		}

		redraw = (should_redraw == WIDGET_REDRAW);
	}

	form_finish(&form);
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
	arrfree(to_redact->body);
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

		pthread_mutex_lock(&state->rooms_mutex);
		struct room *room = shget(state->rooms, sync_room.id);
		pthread_mutex_unlock(&state->rooms_mutex);

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
					LOCK_IF_GROW(timeline->buf, &room->realloc_or_modify_mutex);

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
				pthread_mutex_lock(&state->rooms_mutex);
				shput(state->rooms, sync_room.id, room);
				pthread_mutex_unlock(&state->rooms_mutex);

				/* Signal that a new room was added. */
				uintptr_t ptr = (uintptr_t) NULL;
				write(state->thread_comm_pipe[PIPE_WRITE], &ptr, sizeof(ptr));
			} else {
				room_destroy(room);
			}
		} else {
			uintptr_t ptr = (uintptr_t) room;
			write(state->thread_comm_pipe[PIPE_WRITE], &ptr, sizeof(ptr));
		}
	}

	cache_save_txn_finish(&txn);
}

int
hm_init(struct state *state) {
	/* sh_new_strdup is important to avoid use after frees. Must call these
	 * 2 functions here before the syncer thread starts. */
	sh_new_strdup(state->rooms);
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
	  .rooms_mutex = PTHREAD_MUTEX_INITIALIZER,
	  .thread_comm_pipe = {-1, -1}
	  };

	if (!ERRLOG(
		  matrix_global_init() == 0, "Failed to initialize matrix globals.\n")
		&& !ERRLOG(
		  pipe(state.thread_comm_pipe) == 0, "Failed to initialize pipe.\n")
		&& !ERRLOG(
		  cache_init(&state.cache) == 0, "Failed to initialize database.\n")
		&& !ERRLOG(ui_init() == 0, "Failed to initialize UI.\n")
		&& !ERRLOG(pthread_create(
					 &state.threads[THREAD_QUEUE], NULL, queue_listener, &state)
					 == 0,
		  "Failed to initialize queue thread.\n")
		&& (login(&state)) == 0
		&& (hm_init(&state)) == 0
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
