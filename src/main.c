/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "app/state.h"
#include "util/log.h"

#include <assert.h>
#include <langinfo.h>
#include <locale.h>

enum { FD_TTY = 0, FD_RESIZE, FD_PIPE, FD_MAX };

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

	for (size_t i = 0, len = shlenu(state->state_rooms.rooms); i < len; i++) {
		room_destroy(state->state_rooms.rooms[i].value);
	}

	shfree(state->state_rooms.rooms);
	shfree(state->state_rooms.orphaned_rooms);

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
ui_loop(struct state *state) {
	assert(state);

	struct tb_event event = {0};
	struct pollfd fds[FD_MAX] = {0};

	get_fds(state, fds);

	struct tab_room tab_room = {0};
	tab_room_init(&tab_room);

	tab_room_reset_rooms(&tab_room, &state->state_rooms);

	for (bool redraw = true;;) {
		if (redraw) {
			redraw = false;

			tb_clear();
			tb_hide_cursor();

			if (tab_room.selected_room) {
				reset_room_buffer(tab_room.selected_room->value);
			}

			tab_room_redraw(&tab_room);

			tb_present();
		}

		int fds_with_data = poll(fds, FD_MAX, -1);

		if (fds_with_data > 0 && (fds[FD_PIPE].revents & POLLIN)) {
			fds_with_data--;

			uintptr_t data = 0;

			ssize_t ret = safe_read(
			  state->thread_comm_pipe[PIPE_READ], &data, sizeof(data));

			assert(ret == 0);
			assert(data);

			/* Ensure that we redraw if we had changes. */
			redraw = handle_accumulated_sync(&state->state_rooms, &tab_room,
			  /* NOLINTNEXTLINE(performance-no-int-to-ptr) */
			  (struct accumulated_sync_data *) data);

			state->sync_cond_signaled = true;
			pthread_cond_signal(&state->sync_cond);
		}

		if (fds_with_data <= 0 || (tb_poll_event(&event)) != TB_OK) {
			continue;
		}

		if (event.key == TB_KEY_CTRL_C) {
			/* Ensure that the syncer thread never deadlocks if we break here.
			 * TODO verify that this works. */
			state->sync_cond_signaled = true;
			pthread_cond_signal(&state->sync_cond);

			break;
		}

		if (handle_tab_room(state, &tab_room, &event) == WIDGET_REDRAW) {
			redraw = true;
		}
	}

	tab_room_finish(&tab_room);
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

			if ((safe_read(
				  state->thread_comm_pipe[PIPE_READ], &code, sizeof(code)))
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

		return ret;
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

	SHMAP_INIT(state->state_rooms.rooms);
	/* No need to call SHMAP_INIT on state->state_rooms.orphaned_rooms as it
	 * just takes pointers from state->state_rooms.rooms. */

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
