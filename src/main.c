/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "login_form.h"
#include "queue_callbacks.h"
#include "room_ds.h"
#include "ui.h"

#include <assert.h>
#include <langinfo.h>
#include <locale.h>

enum widget {
	WIDGET_INPUT = 0,
	WIDGET_FORM,
	WIDGET_TREE,
};

enum tab { TAB_LOGIN = 0, TAB_HOME, TAB_ROOM };

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

	printf("%s\n", "Any errors will been logged to '" LOG_PATH "'");
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
	case TB_KEY_MOUSE_RELEASE:
		return message_buffer_handle_event(
		  buf, MESSAGE_BUFFER_SELECT, event->x, event->y);
		break;
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

static int
tab_room_set(struct tab_room *tab_room, size_t index) {
	assert(tab_room);

	int ret = -1;

	pthread_mutex_lock(tab_room->rooms_mutex);
	if ((shlenu(tab_room->rooms)) > index) {
		tab_room->current_room = (struct tab_room_current_room) {
		  .index = index,
		  .id = tab_room->rooms[index].key,
		  .room = tab_room->rooms[index].value,
		};

		ret = 0;
	}
	pthread_mutex_unlock(tab_room->rooms_mutex);

	return ret;
}

static void
fill_old_events(struct room *room) {
	assert(room);

	struct widget_points points = {0};
	tab_room_get_buffer_points(&points);

	struct message **buf = room->timelines[TIMELINE_BACKWARD].buf;
	size_t len = room->timelines[TIMELINE_BACKWARD].len;

	for (size_t i = len; i > 0; i--) {
		if (!buf[i - 1]->redacted) {
			message_buffer_insert(
			  &room->buffer, room->members, &points, buf[i - 1]);
		}
	}
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
			  &room->buffer, room->members, &points, buf[already_consumed]);
		}
	}

	pthread_mutex_unlock(&room->realloc_or_modify_mutex);

	return already_consumed;
}

static size_t
reset_message_buffer_if_recalculate(struct room *room) {
	assert(room);

	size_t consumed = 0;

	struct widget_points points = {0};
	tab_room_get_buffer_points(&points);

	pthread_mutex_lock(&room->realloc_or_modify_mutex);
	bool recalculate
	  = message_buffer_should_recalculate(&room->buffer, &points);
	pthread_mutex_unlock(&room->realloc_or_modify_mutex);

	if (recalculate) {
		message_buffer_zero(&room->buffer);
		fill_old_events(room);
		consumed = fill_new_events(room, 0);
		message_buffer_ensure_sane_scroll(&room->buffer);
	}

	return consumed;
}

static enum widget_error
handle_tab_room(
  struct state *state, struct tab_room *tab_room, struct tb_event *event) {
	enum widget_error ret = WIDGET_NOOP;
	struct room *room = tab_room->current_room.room;

	if (!room) {
		return ret;
	}

	if (event->type == TB_EVENT_RESIZE) {
		size_t consumed = reset_message_buffer_if_recalculate(room);

		if (consumed > 0) {
			tab_room->current_room.room->already_consumed = consumed;
		}

		return WIDGET_REDRAW;
	}

	if (event->type == TB_EVENT_MOUSE) {
		pthread_mutex_lock(&room->realloc_or_modify_mutex);
		ret = handle_message_buffer(&room->buffer, event);
		pthread_mutex_unlock(&room->realloc_or_modify_mutex);
	} else {
		bool enter_pressed = false;

		/* Shortcuts */
		if (event->type == TB_EVENT_KEY && event->mod & TB_MOD_CTRL) {
			int room_change_ret = -1;

			switch (event->key) {
			case CTRL('n'):
				room_change_ret
				  = tab_room_set(tab_room, tab_room->current_room.index + 1);
				break;
			case CTRL('p'):
				room_change_ret
				  = tab_room_set(tab_room, tab_room->current_room.index - 1);
				break;
			}

			if (room_change_ret == 0) {
				event->type = TB_EVENT_RESIZE;
				return handle_tab_room(state, tab_room, event);
			}
		}

		switch (tab_room->widget) {
		case TAB_ROOM_INPUT:
			ret = handle_input(tab_room->input, event, &enter_pressed);

			if (enter_pressed) {
				char *buf = input_buf(tab_room->input);

				/* Empty field. */
				if (!buf) {
					break;
				}

				struct sent_message *message = malloc(sizeof(*message));

				*message = (struct sent_message) {
				  .has_reply = false,
				  .reply_index = 0,
				  .buf = buf,
				  .room_id = tab_room->current_room.id,
				};

				lock_and_push(
				  state, queue_item_alloc(QUEUE_ITEM_MESSAGE, message));

				ret = input_handle_event(tab_room->input, INPUT_CLEAR);
			}
			break;
		default:
			break;
		}
	}

	return ret;
}

static void
ui_loop(struct state *state) {
	assert(state);

	enum tab tab = TAB_ROOM;

	struct tb_event event = {0};
	struct pollfd fds[FD_MAX] = {0};

	get_fds(state, fds);

	struct input input = {0};

	if ((input_init(&input, TB_DEFAULT, false)) != 0) {
		return;
	}

	struct tab_room tab_room = {
	  .widget = TAB_ROOM_INPUT,
	  .input = &input,
	  .rooms = state->rooms,
	  .rooms_mutex = &state->rooms_mutex,
	};

	if ((tab_room_set(&tab_room, 0)) == 0) {
		fill_old_events(tab_room.current_room.room);
	}

	for (bool redraw = true;;) {
		if (redraw) {
			redraw = false;

			tb_clear();

			switch (tab) {
			case TAB_HOME:
				break;
			case TAB_ROOM:
				tab_room_redraw(&tab_room);
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
				&& read_room == (uintptr_t) tab_room.current_room.room) {
				if (!tab_room.current_room.room) {
					int ret = tab_room_set(&tab_room, 0);
					assert(ret == 0);
				}

				tab_room.current_room.room->already_consumed
				  = fill_new_events(tab_room.current_room.room,
					tab_room.current_room.room->already_consumed);
				redraw = true;
			}
		}

		if (fds_with_data <= 0 || (tb_poll_event(&event)) != TB_OK) {
			continue;
		}

		enum widget_error ret = WIDGET_NOOP;

		if (event.key == TB_KEY_CTRL_C) {
			break;
		}

		switch (tab) {
		case TAB_HOME:
			// handle_tab_home();
			break;
		case TAB_ROOM:
			ret = handle_tab_room(state, &tab_room, &event);
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
room_put_member(struct room *room, char *mxid, char *username) {
	assert(room);
	assert(mxid);

	/* If len < 1 then displayname has been removed. */
	uint32_t *username_or_stripped_mxid
	  = (username && (strnlen(username, 1)) > 0) ? buf_to_uint32_t(username, 0)
												 : mxid_to_uint32_t(mxid);

	assert(username_or_stripped_mxid);

	ptrdiff_t tmp = 0;
	ptrdiff_t sh_index = shgeti_ts(room->members, mxid, tmp);

	if (sh_index < 0) {
		uint32_t **usernames = NULL;
		arrput(usernames, username_or_stripped_mxid);
		shput(room->members, mxid, usernames);
	} else {
		arrput(room->members[sh_index].value, username_or_stripped_mxid);
	}

	return 0;
}

static int
room_put_message_event(struct room *room, struct timeline *timeline,
  uint64_t index, struct matrix_timeline_event *event) {
	assert(event->base.sender);
	assert(event->message.body);
	assert(event->type == MATRIX_ROOM_MESSAGE);

	ptrdiff_t tmp = 0;
	uint32_t **usernames = shget_ts(room->members, event->base.sender, tmp);
	size_t usernames_len = arrlenu(usernames);

	assert(usernames_len);

	if (usernames_len == 0) {
		return -1;
	}

	struct message *message = message_alloc(event->message.body,
	  event->base.sender, usernames_len - 1, index, NULL, false);

	if (!message) {
		return -1;
	}

	/* This is safe as the reader thread will have the old value
	 * of len stored and not access anything beyond that. */
	arrput(timeline->buf, message);

	return 0;
}

static int
populate_room_users(struct state *state, char *room_id) {
	assert(state);
	assert(room_id);

	struct cache_iterator iterator = {0};

	ptrdiff_t tmp = 0;
	struct room *room = shget_ts(state->rooms, room_id, tmp);
	struct cache_iterator_member member = {0};

	assert(room);

	int ret = cache_iterator_member(&state->cache, &iterator, room_id, &member);

	if (ret != MDB_SUCCESS) {
		fprintf(stderr, "Failed to create member iterator for room '%s': %s\n",
		  room_id, mdb_strerror(ret));
		return ret;
	}

	while ((cache_iterator_next(&iterator)) == MDB_SUCCESS) {
		room_put_member(room, member.mxid, member.username);
		matrix_json_delete(member.json);
	}

	cache_iterator_finish(&iterator);

	return 0;
}

static int
populate_room_from_cache(struct state *state, char *room_id) {
	assert(state);
	assert(room_id);

	struct cache_iterator iterator = {0};

	populate_room_users(state, room_id);

	ptrdiff_t tmp = 0;
	struct room *room = shget_ts(state->rooms, room_id, tmp);
	struct cache_iterator_event event = {0};

	assert(room);

	const uint64_t num_paginate = 50;

	int ret = cache_iterator_events(
	  &state->cache, &iterator, room_id, &event, (uint64_t) -1, num_paginate);

	if (ret != MDB_SUCCESS) {
		fprintf(stderr, "Failed to create events iterator for room '%s': %s\n",
		  room_id, mdb_strerror(ret));
		return ret;
	}

	struct timeline *timeline = &room->timelines[TIMELINE_BACKWARD];

	while ((cache_iterator_next(&iterator)) == MDB_SUCCESS) {
		if (event.event.type == MATRIX_ROOM_MESSAGE) {
			room_put_message_event(room, timeline, event.index, &event.event);
		}

		matrix_json_delete(event.json);
	}

	timeline->len = arrlenu(timeline->buf);
	cache_iterator_finish(&iterator);

	return ret;
}

static int
populate_from_cache(struct state *state) {
	assert(state);

	struct cache_iterator iterator = {0};
	char *id = NULL;

	int ret = cache_iterator_rooms(&state->cache, &iterator, &id);

	if (ret != MDB_SUCCESS) {
		fprintf(
		  stderr, "Failed to create room iterator: %s\n", mdb_strerror(ret));
		return -1;
	}

	while ((cache_iterator_next(&iterator)) == MDB_SUCCESS) {
		assert(id);
		struct room_info *info = cache_room_info(&state->cache, id);

		if (info) {
			struct room *room = room_alloc(info);

			if (room) {
				shput(state->rooms, id, room);
				populate_room_from_cache(state, id);
			} else {
				room_info_destroy(info);
			}
		}

		free(id);
		id = NULL; /* Fix false positive in static analyzers, id is
					* reassigned by cache_iterator_next. */
	}

	cache_iterator_finish(&iterator);

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

static enum widget_error
handle_tab_login(
  struct state *state, struct tab_login *login, struct tb_event *event) {
	assert(login);
	assert(event);

	if (event->type == TB_EVENT_RESIZE) {
		return WIDGET_REDRAW;
	}

	if (login->logging_in) {
		return WIDGET_NOOP;
	}

	switch (event->key) {
	case TB_KEY_ARROW_UP:
		return form_handle_event(&login->form, FORM_UP);
	case TB_KEY_ARROW_DOWN:
		return form_handle_event(&login->form, FORM_DOWN);
	case TB_KEY_ENTER:
		if (!login->form.button_is_selected) {
			return WIDGET_NOOP;
		}

		if ((login_with_info(state, &login->form)) == 0) {
			login->error = NULL;
			login->logging_in = true;
		} else {
			login->error = "Invalid Information";
		}

		return WIDGET_REDRAW;
	default:
		{
			struct input *input = form_current_input(&login->form);

			if (input) {
				return handle_input(input, event, NULL);
			}

			return WIDGET_NOOP;
		}
	}
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

	struct tab_login login = {0};

	if ((form_init(&login.form, COLOR_BLUE)) == -1) {
		return ret;
	}

	struct tb_event event = {0};

	struct pollfd fds[FD_MAX] = {0};
	get_fds(state, fds);

	for (bool redraw = true;;) {
		if (redraw) {
			redraw = false;

			tb_clear();
			tab_login_redraw(&login);
			tb_present();
		}

		int fds_with_data = poll(fds, FD_MAX, -1);

		if (fds_with_data > 0 && (fds[FD_PIPE].revents & POLLIN)) {
			fds_with_data--;

			enum matrix_code code = MATRIX_SUCCESS;

			if ((read(state->thread_comm_pipe[PIPE_READ], &code, sizeof(code)))
				== 0) {
				login.logging_in = false;

				if (code == MATRIX_SUCCESS) {
					login.error = NULL;
					ret = 0;
				} else {
					assert(code != MATRIX_CODE_MAX);

					if (code < MATRIX_CODE_MAX) {
						login.error = matrix_strerror(code);
					} else {
						assert(code == (enum matrix_code) LOGIN_DB_FAIL);

						/* Clear the unsaved access token. */
						int ret_logout = matrix_logout(state->matrix);

						assert(ret_logout == 0);
						(void) ret_logout;

						login.error = "Failed to save to database";
					}
				}

				tb_clear();
				tab_login_redraw(&login);
				tb_present();

				if (ret == 0) {
					break;
				}
			}
		}

		if (fds_with_data <= 0) {
			continue;
		}

		bool ctrl_c_pressed = false;

		while ((tb_peek_event(&event, 0)) == TB_OK) {
			if (event.key == TB_KEY_CTRL_C) {
				ret
				  = -1; /* Transfer cancelled and thread killed in cleanup() */
				ctrl_c_pressed = true;
				break;
			}

			if ((handle_tab_login(state, &login, &event)) == WIDGET_REDRAW) {
				redraw = true;
			}
		}

		if (ctrl_c_pressed) {
			break;
		}
	}

	form_finish(&login.form);
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

	int ret = 0;

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

		if ((ret = cache_save_txn_init(&state->cache, &txn)) != MDB_SUCCESS) {
			fprintf(stderr, "Failed to start save txn for room '%s': %s\n",
			  sync_room.id, mdb_strerror(ret));
			continue;
		}

		if ((ret = cache_set_room_dbs(&txn, &sync_room)) != MDB_SUCCESS) {
			fprintf(stderr, "Failed to open room DBs for room '%s': %s\n",
			  sync_room.id, mdb_strerror(ret));
			continue;
		}

		if ((ret = cache_save_room(&txn, &sync_room)) != MDB_SUCCESS) {
			fprintf(stderr, "Failed to save room '%s': %s\n", sync_room.id,
			  mdb_strerror(ret));
			continue;
		}

		struct matrix_sync_event event;

		pthread_mutex_lock(&state->rooms_mutex);
		ptrdiff_t tmp = 0;
		struct room *room = shget_ts(state->rooms, sync_room.id, tmp);
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
			enum cache_save_error cache_ret = CACHE_FAIL;

			if ((cache_ret = cache_save_event(&txn, &event)) == CACHE_FAIL) {
				const char *id = matrix_sync_event_id(&event);

				if (id) {
					fprintf(stderr, "Failed to save event '%s' for room '%s'\n",
					  id, sync_room.id);
				}

				continue;
			}

			switch (event.type) {
			case MATRIX_EVENT_EPHEMERAL:
				break;
			case MATRIX_EVENT_STATE:
				switch (event.state.type) {
				case MATRIX_ROOM_MEMBER:
					/* We lock for every member here, but it's not a big
					 * issue since member events are very rare and we won't
					 * have more than 1-2 of them per-sync
					 * except for large syncs like the initial sync. */
					pthread_mutex_lock(&room->realloc_or_modify_mutex);
					room_put_member(room, event.state.base.sender,
					  event.state.member.displayname);
					pthread_mutex_unlock(&room->realloc_or_modify_mutex);
				default:
					break;
				}
				break;
			case MATRIX_EVENT_TIMELINE:
				switch (event.timeline.type) {
				case MATRIX_ROOM_MESSAGE:
					LOCK_IF_GROW(timeline->buf, &room->realloc_or_modify_mutex);
					room_put_message_event(
					  room, timeline, index, &event.timeline);
					break;
				case MATRIX_ROOM_REDACTION:
					if (cache_ret == CACHE_GOT_REDACTION) {
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

		/* Save here to fetch the topic/name below. */
		cache_save_txn_finish(&txn);

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

	/* Set the next_batch key at the end in case we somehow exited during the
	 * above loop. This will ensure that we receive the left out events on the
	 * next boot. */
	if ((ret = cache_auth_set(
		   &state->cache, DB_KEY_NEXT_BATCH, response->next_batch))
		!= MDB_SUCCESS) {
		fprintf(stderr, "Failed to save next batch: %s\n", mdb_strerror(ret));
	}
}

static int
redirect_stderr_log(void) {
	const mode_t perms = 0600;
	int fd = open(LOG_PATH, O_CREAT | O_RDWR | O_TRUNC, perms);

	if (fd == -1 || (dup2(fd, STDERR_FILENO)) == -1) {
		return -1;
	}

	/* Duplicated. */
	close(fd);
	return 0;
}

static int
ui_init(void) {
	int ret = tb_init();

	if (ret == TB_OK) {
		tb_set_input_mode(TB_INPUT_ALT | TB_INPUT_MOUSE);
		tb_set_output_mode(TB_OUTPUT_256);
	}

	return ret;
}

static int
init_everything(struct state *state) {
	if ((matrix_global_init()) != 0) {
		fprintf(stderr, "Failed to initialize matrix globals\n");
		return -1;
	}

	if ((pipe(state->thread_comm_pipe)) != 0) {
		perror("Failed to initialize pipe");
		return -1;
	}

	int ret = -1;

	ret = cache_init(&state->cache);

	if (ret != 0) {
		fprintf(
		  stderr, "Failed to initialize database: %s\n", mdb_strerror(ret));
	}

	ret = pthread_create(
	  &state->threads[THREAD_QUEUE], NULL, queue_listener, state);

	if (ret != 0) {
		errno = ret;
		perror("Failed to initialize queue thread");

		return -1;
	}

	ret = ui_init();

	if (ret != TB_OK) {
		fprintf(stderr, "Failed to initialize UI: %s\n", tb_strerror(ret));
		return -1;
	}

	if ((login(state)) != 0) {
		fprintf(stderr, "Login cancelled\n");
		return -1;
	}

	SHMAP_INIT(state->rooms);

	ret = populate_from_cache(state);

	if (ret != 0) {
		fprintf(stderr, "Failed to populate rooms from cache\n");
		return -1;
	}

	ret = pthread_create(&state->threads[THREAD_SYNC], NULL, syncer, state);

	if (ret != 0) {
		errno = ret;
		perror("Failed to initialize syncer thread");

		return -1;
	}

	return 0;
}

int
main() {
	if (!(setlocale(LC_ALL, ""))) {
		fprintf(stderr, "Failed to set locale\n");
		return EXIT_FAILURE;
	}

	if ((strcmp("UTF-8", nl_langinfo(CODESET)) != 0)) {
		fprintf(stderr, "Locale is not UTF-8\n");
		return EXIT_FAILURE;
	}

	if ((redirect_stderr_log()) == -1) {
		perror("Failed to open log file '" LOG_PATH "'");
		return EXIT_FAILURE;
	}

	struct state state = {
	  .cond = PTHREAD_COND_INITIALIZER,
	  .mutex = PTHREAD_MUTEX_INITIALIZER,
	  .rooms_mutex = PTHREAD_MUTEX_INITIALIZER,
	  .thread_comm_pipe = {-1, -1},
	};

	if ((init_everything(&state)) == 0) {
		ui_loop(&state); /* Blocks forever. */

		cleanup(&state);
		return EXIT_SUCCESS;
	}

	cleanup(&state);
	return EXIT_FAILURE;
}
