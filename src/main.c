/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "app/queue_callbacks.h"
#include "app/room_ds.h"
#include "app/state.h"
#include "ui/login_form.h"
#include "ui/tab_room.h"
#include "ui/ui.h"
#include "util/log.h"

#include <assert.h>
#include <langinfo.h>
#include <locale.h>

enum widget {
	WIDGET_INPUT = 0,
	WIDGET_FORM,
	WIDGET_TREE,
};

enum {
	EVENTS_IN_TIMELINE = MATRIX_ROOM_MESSAGE | MATRIX_ROOM_ATTACHMENT,
	STATE_IN_TIMELINE
	= MATRIX_ROOM_MEMBER | MATRIX_ROOM_NAME | MATRIX_ROOM_TOPIC
};

enum { FD_TTY = 0, FD_RESIZE, FD_PIPE, FD_MAX };

enum tab { TAB_LOGIN = 0, TAB_HOME, TAB_ROOM };

struct accumulated_sync_room {
	enum matrix_room_type type;
	struct room *room;
	char *id;
};

struct accumulated_space_event {
	enum cache_deferred_ret status;
	const char *parent;
	const char *child;
};

/* We pass this struct to the main thread after processing each sync response.
 * This saves us from most threading related issues but wastes a bit more
 * space as temp arrays are constructed instead of using the data in-place
 * when consuming an iterator. */
struct accumulated_sync_data {
	struct accumulated_sync_room
	  *rooms; /* Array of rooms that received events. */
	/* Array of space-related events which might require shifting nodes
	 * in the treeview. */
	struct accumulated_space_event *space_events;
};

enum widget_error
handle_tab_room(
  struct state *state, struct tab_room *tab_room, struct tb_event *event);
enum widget_error
handle_tab_login(
  struct state *state, struct tab_login *login, struct tb_event *event);

static void
sync_cb(struct matrix *matrix, struct matrix_sync_response *response);

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
		pthread_cond_signal(&state->queue_cond);
		pthread_join(state->threads[THREAD_QUEUE], NULL);
	}

	for (size_t i = 0; i < PIPE_MAX; i++) {
		if (state->thread_comm_pipe[i] != -1) {
			close(state->thread_comm_pipe[i]);
		}
	}

	pthread_cond_destroy(&state->queue_cond);
	pthread_mutex_destroy(&state->queue_mutex);

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
	shfree(state->orphaned_rooms);

	memset(state, 0, sizeof(*state));

	printf("%s '%s'\n", "Debug information has been logged to", log_path());

	log_mutex_destroy();
}

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

static void *
queue_listener(void *arg) {
	struct state *state = arg;

	while (!state->done) {
		pthread_mutex_lock(&state->queue_mutex);

		struct queue_item *item = NULL;

		while (!(item = queue_pop_head(&state->queue)) && !state->done) {
			pthread_cond_wait(&state->queue_cond, &state->queue_mutex);
		}

		pthread_mutex_unlock(&state->queue_mutex);

		/* Could've been set to false in between the above function calls. */
		if (!state->done) {
			queue_callbacks[item->type].cb(state, item->data);
		}

		queue_callbacks[item->type].free(item->data);
		free(item);
	}

	pthread_exit(NULL);
}

static void
reset_room_buffer(struct room *room) {
	struct widget_points points = {0};
	tab_room_get_buffer_points(&points);

	room_maybe_reset_and_fill_events(room, &points);
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

static void
state_reset_orphans(struct state *state) {
	assert(state);

	shfree(state->orphaned_rooms);

	struct {
		char *key;
		bool value;
	} *children = NULL;

	/* Collect a hashmap of all rooms that are a child of any other room. */
	for (size_t i = 0, len = shlenu(state->rooms); i < len; i++) {
		struct room *room = state->rooms[i].value;

		for (size_t j = 0, children_len = shlenu(room->children);
			 j < children_len; j++) {
			shput(children, room->children[j].key, true);
		}
	}

	/* Now any rooms which aren't children of any other room are orphans. */
	for (size_t i = 0, len = shlenu(state->rooms); i < len; i++) {
		if (shget(children, state->rooms[i].key)) {
			continue; /* A child */
		}

		shput(
		  state->orphaned_rooms, state->rooms[i].key, state->rooms[i].value);
	}

	shfree(children);
}

static bool
handle_accumulated_sync(struct state *state, struct tab_room *tab_room,
  struct accumulated_sync_data *data) {
	assert(state);
	assert(tab_room);
	assert(data);

	bool any_tree_changes = false;
	bool any_room_events = false;

	for (size_t i = 0, len = arrlenu(data->rooms); i < len; i++) {
		struct accumulated_sync_room *room = &data->rooms[i];

		assert(room->id);
		assert(room->room);

		switch (room->type) {
		case MATRIX_ROOM_LEAVE:
		case MATRIX_ROOM_JOIN:
		case MATRIX_ROOM_INVITE:
			break;
		default:
			assert(0);
		}

		if (!(rooms_get_room(state->rooms, room->id))) {
			any_tree_changes = true; /* New room added */
			/* This doesn't need locking as the syncer thread waits until
			 * we use all the accumulated data (this function). */
			shput(state->rooms, room->id, room->room);
		}

		if (tab_room->selected_room
			&& strcmp(room->id, tab_room->selected_room->key) == 0) {
			assert(room->room == tab_room->selected_room->value);
			any_room_events = true;
		}
	}

	for (size_t i = 0, len = arrlenu(data->space_events); i < len; i++) {
		struct accumulated_space_event *event = &data->space_events[i];

		assert(event->parent);
		assert(event->child);

		struct room *room
		  = rooms_get_room(state->rooms, noconst(event->parent));
		assert(room);

		switch (event->status) {
		case CACHE_DEFERRED_ADDED:
			room_add_child(room, noconst(event->child));
			break;
		case CACHE_DEFERRED_REMOVED:
			room_remove_child(room, noconst(event->child));
			break;
		default:
			assert(0);
		}
	}

	if (any_tree_changes || arrlenu(data->space_events) > 0) {
		state_reset_orphans(state);
		tab_room_reset_rooms(tab_room, state);
		return true;
	}

	return any_tree_changes || any_room_events;
}

static void
ui_loop(struct state *state) {
	assert(state);

	enum tab tab = TAB_ROOM;

	struct tb_event event = {0};
	struct pollfd fds[FD_MAX] = {0};

	get_fds(state, fds);

	struct tab_room tab_room = {0};
	tab_room_init(&tab_room);

	tab_room_reset_rooms(&tab_room, state);

	for (bool redraw = true;;) {
		if (redraw) {
			redraw = false;

			tb_clear();
			tb_hide_cursor();

			switch (tab) {
			case TAB_HOME:
				break;
			case TAB_ROOM:
				if (tab_room.selected_room) {
					reset_room_buffer(tab_room.selected_room->value);
				}

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

			uintptr_t data = 0;

			ssize_t ret
			  = read(state->thread_comm_pipe[PIPE_READ], &data, sizeof(data));

			assert(ret == 0);
			assert(data);

			/* Ensure that we redraw if we had changes. */
			redraw = handle_accumulated_sync(
			  /* NOLINTNEXTLINE(performance-no-int-to-ptr) */
			  state, &tab_room, (struct accumulated_sync_data *) data);

			state->sync_cond_signaled = true;
			pthread_cond_signal(&state->sync_cond);
		}

		if (fds_with_data <= 0 || (tb_poll_event(&event)) != TB_OK) {
			continue;
		}

		enum widget_error ret = WIDGET_NOOP;

		if (event.key == TB_KEY_CTRL_C) {
			/* Ensure that the syncer thread never deadlocks if we break here.
			 * TODO verify that this works. */
			state->sync_cond_signaled = true;
			pthread_cond_signal(&state->sync_cond);

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

	tab_room_finish(&tab_room);
}

static int
populate_room_users(struct state *state, const char *room_id) {
	assert(state);
	assert(room_id);

	struct cache_iterator iterator = {0};

	struct room *room = rooms_get_room(state->rooms, noconst(room_id));
	struct cache_iterator_member member = {0};

	assert(room);

	int ret = cache_iterator_member(&state->cache, &iterator, room_id, &member);

	if (ret != MDB_SUCCESS) {
		LOG(LOG_ERROR, "Failed to create member iterator for room '%s': %s",
		  room_id, mdb_strerror(ret));
		return ret;
	}

	while ((cache_iterator_next(&iterator)) == MDB_SUCCESS) {
		room_put_member(room, member.mxid, member.username);
	}

	cache_iterator_finish(&iterator);

	return 0;
}

static int
populate_room_from_cache(struct state *state, const char *room_id) {
	assert(state);
	assert(room_id);

	struct cache_iterator iterator = {0};

	populate_room_users(state, room_id);

	struct room *room = rooms_get_room(state->rooms, noconst(room_id));
	struct cache_iterator_event event = {0};

	assert(room);

	const uint64_t num_paginate = 50;

	int ret = cache_iterator_events(&state->cache, &iterator, room_id, &event,
	  (uint64_t) -1, num_paginate, EVENTS_IN_TIMELINE, STATE_IN_TIMELINE);

	if (ret != MDB_SUCCESS) {
		LOG(LOG_ERROR, "Failed to create events iterator for room '%s': %s",
		  room_id, mdb_strerror(ret));
		return ret;
	}

	while ((cache_iterator_next(&iterator)) == MDB_SUCCESS) {
		struct matrix_sync_event *sync_event = &event.event;
		assert(sync_event->type != MATRIX_EVENT_EPHEMERAL);

		room_put_event(room, sync_event, true, event.index, (uint64_t) -1);
	}

	cache_iterator_finish(&iterator);

	return ret;
}

static int
populate_from_cache(struct state *state) {
	assert(state);

	struct cache_iterator iterator = {0};
	const char *id = NULL;

	int ret = cache_iterator_rooms(&state->cache, &iterator, &id);

	if (ret != MDB_SUCCESS) {
		LOG(LOG_ERROR, "Failed to create room iterator: %s", mdb_strerror(ret));
		return -1;
	}

	while ((cache_iterator_next(&iterator)) == MDB_SUCCESS) {
		assert(id);

		struct room_info info = {0};

		if ((ret = cache_room_info_init(&state->cache, &info, id))
			!= MDB_SUCCESS) {
			LOG(LOG_ERROR, "Failed to get room info for room '%s': %s", id,
			  mdb_strerror(ret));
			cache_iterator_finish(&iterator);

			return -1;
		}

		struct room *room = room_alloc(info);
		assert(room);

		shput(state->rooms, noconst(id), room);
		populate_room_from_cache(state, id);
	}

	cache_iterator_finish(&iterator);

	struct cache_iterator_space space = {0};
	ret = cache_iterator_spaces(&state->cache, &iterator, &space);

	if (ret != MDB_SUCCESS) {
		LOG(
		  LOG_ERROR, "Failed to create spaces iterator: %s", mdb_strerror(ret));
		return -1;
	}

	while ((cache_iterator_next(&iterator)) == MDB_SUCCESS) {
		assert(space.id);

		struct room *space_room = shget(state->rooms, noconst(space.id));

		if (!space_room) {
			LOG(LOG_WARN, "Got unknown space '%s'", space.id);
			assert(0); /* TODO */
			continue;
		}

		assert(space_room->info.is_space);

		/* Fetch all children. */
		while ((cache_iterator_next(&space.children_iterator)) == MDB_SUCCESS) {
			assert(space.child_id);

			struct room *space_child
			  = shget(state->rooms, noconst(space.child_id));

			/* TOOD change .children to just be strings so we can handle
			 * newly joined rooms. */
			if (space_child) {
				LOG(LOG_MESSAGE, "Got %s '%s' in space '%s'",
				  space_child->info.is_space ? "space" : "room", space.child_id,
				  space.id);
			} else {
				LOG(LOG_MESSAGE, "Got unknown room '%s' in space '%s'",
				  space.child_id, space.id);
			}

			room_add_child(space_room, noconst(space.child_id));
		}

		/* No need to finish the children iterator since it only borrows a
		 * cursor from the parent iterator, which is freed when the parent is
		 * finished. */
	}

	cache_iterator_finish(&iterator);

	state_reset_orphans(state);

	return 0;
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
			tb_hide_cursor();
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
					assert(code < MATRIX_CODE_MAX);
					login.error = matrix_strerror(code);
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

static void
sync_cb(struct matrix *matrix, struct matrix_sync_response *response) {
	assert(matrix);
	assert(response);

	struct state *state = matrix_userp(matrix);

	assert(state);

	struct matrix_room sync_room;
	struct cache_save_txn txn = {0};

	struct accumulated_sync_data data = {0};
	struct cache_deferred_space_event *deferred_events = NULL;

	int ret = 0;

	while ((matrix_sync_room_next(response, &sync_room)) == 0) {
		switch (sync_room.type) {
		case MATRIX_ROOM_LEAVE:
		case MATRIX_ROOM_JOIN:
		case MATRIX_ROOM_INVITE:
			break;
		default:
			assert(0);
		}

		if ((ret = cache_save_txn_init(&state->cache, &txn, sync_room.id))
			!= MDB_SUCCESS) {
			LOG(LOG_ERROR, "Failed to start save txn for room '%s': %s",
			  sync_room.id, mdb_strerror(ret));
			continue;
		}

		if ((ret = cache_set_room_dbs(&txn, &sync_room)) != MDB_SUCCESS) {
			LOG(LOG_ERROR, "Failed to open room DBs for room '%s': %s",
			  sync_room.id, mdb_strerror(ret));
			cache_save_txn_finish(&txn);
			continue;
		}

		if ((ret = cache_save_room(&txn, &sync_room)) != MDB_SUCCESS) {
			LOG(LOG_ERROR, "Failed to save room '%s': %s", sync_room.id,
			  mdb_strerror(ret));
			cache_save_txn_finish(&txn);
			continue;
		}

		struct matrix_sync_event event;

		struct room *room = rooms_get_room(state->rooms, sync_room.id);

		bool room_needs_info = !room;

		if (room_needs_info) {
			/* TODO make room_alloc take a timeline so we don't need this hack
			 */
			room = room_alloc((struct room_info) {0});
			assert(room);
		}

		while ((matrix_sync_event_next(&sync_room, &event)) == 0) {
			uint64_t index = 0;
			uint64_t redaction_index = 0;

			switch ((cache_save_event(
			  &txn, &event, &index, &redaction_index, &deferred_events))) {
			case CACHE_EVENT_SAVED:
				room_put_event(room, &event, false, index, redaction_index);
				break;
			case CACHE_EVENT_IGNORED:
			case CACHE_EVENT_DEFERRED:
				break;
			default:
				assert(0);
			}
		}

		cache_save_txn_finish(&txn);

		if (room_needs_info) {
			ret
			  = cache_room_info_init(&state->cache, &room->info, sync_room.id);

			if (ret != MDB_SUCCESS) {
				LOG(LOG_ERROR, "Failed to get room info for room '%s': %s",
				  sync_room.id, mdb_strerror(ret));
				assert(0);
			}
		}

		arrput(data.rooms,
		  ((struct accumulated_sync_room) {
			.type = sync_room.type, .room = room, .id = sync_room.id}));
	}

	for (size_t i = 0, len = arrlenu(deferred_events); i < len; i++) {
		enum cache_deferred_ret deferred_ret
		  = cache_process_deferred_event(&state->cache, &deferred_events[i]);

		if (deferred_ret == CACHE_DEFERRED_FAIL) {
			continue;
		}

		arrput(data.space_events, ((struct accumulated_space_event) {
									.status = deferred_ret,
									.parent = deferred_events[i].parent,
									.child = deferred_events[i].child,
								  }));
	}

	arrfree(deferred_events);

	/* Set the next_batch key at the end in case we somehow crashed during the
	 * above loop. This will ensure that we receive the left out events on the
	 * next boot. */
	if ((ret = cache_auth_set(
		   &state->cache, DB_KEY_NEXT_BATCH, response->next_batch))
		!= MDB_SUCCESS) {
		LOG(LOG_ERROR, "Failed to save next batch: %s", mdb_strerror(ret));
		assert(0);
	}

	uintptr_t ptr = (uintptr_t) &data;

	write(state->thread_comm_pipe[PIPE_WRITE], &ptr, sizeof(ptr));

	pthread_mutex_lock(&state->sync_mutex);

	/* Wait until the main thread acknowledges the data passed above. */
	while (!(state->sync_cond_signaled)) {
		pthread_cond_wait(&state->sync_cond, &state->sync_mutex);
	}

	state->sync_cond_signaled = false;

	pthread_mutex_unlock(&state->sync_mutex);

	arrfree(data.rooms);
	arrfree(data.space_events);
}

static int
redirect_stderr_log(void) {
	const mode_t perms = 0600;
	int fd = open(log_path(), O_CREAT | O_RDWR | O_APPEND, perms);

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
		LOG(LOG_WARN, "Failed to initialize matrix globals");
		return -1;
	}

	if ((pipe(state->thread_comm_pipe)) != 0) {
		perror("Failed to initialize pipe");
		return -1;
	}

	int ret = -1;

	ret = cache_init(&state->cache);

	if (ret != 0) {
		LOG(LOG_ERROR, "Failed to initialize database: %s", mdb_strerror(ret));
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
		LOG(LOG_ERROR, "Failed to initialize UI: %s", tb_strerror(ret));
		return -1;
	}

	/* Only main thread active. */
	/* NOLINTNEXTLINE(concurrency-mt-unsafe) */
	if ((login(state)) != 0) {
		LOG(LOG_ERROR, "Login cancelled");
		return -1;
	}

	SHMAP_INIT(state->rooms);
	/* No need to call SHMAP_INIT on state->orphaned_rooms as it just takes
	 * pointers from state->rooms. */

	ret = populate_from_cache(state);

	if (ret != 0) {
		LOG(LOG_ERROR, "Failed to populate rooms from cache");
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
main(void) {
	log_path_set();

	/* Only main thread active. */

	/* NOLINTNEXTLINE(concurrency-mt-unsafe) */
	if (!(setlocale(LC_ALL, ""))) {
		LOG(LOG_ERROR, "Failed to set locale");
		return EXIT_FAILURE;
	}

	/* NOLINTNEXTLINE(concurrency-mt-unsafe) */
	if ((strcmp("UTF-8", nl_langinfo(CODESET)) != 0)) {
		LOG(LOG_ERROR, "Locale is not UTF-8");
		return EXIT_FAILURE;
	}

	if ((redirect_stderr_log()) == -1) {
		LOG(LOG_ERROR, "Failed to open log file '%s': %s", log_path(),
		  /* NOLINTNEXTLINE(concurrency-mt-unsafe) */
		  strerror(errno));
		return EXIT_FAILURE;
	}

	LOG(LOG_MESSAGE, "Initialized");

	struct state state = {
	  .queue_cond = PTHREAD_COND_INITIALIZER,
	  .queue_mutex = PTHREAD_MUTEX_INITIALIZER,
	  .sync_cond = PTHREAD_COND_INITIALIZER,
	  .sync_mutex = PTHREAD_MUTEX_INITIALIZER,
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
