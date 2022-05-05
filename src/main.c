/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "log.h"
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

const char *const root_node_str[NODE_MAX] = {
  [NODE_INVITES] = "Invites",
  [NODE_SPACES] = "Spaces",
  [NODE_DMS] = "DMs",
  [NODE_ROOMS] = "Rooms",
};

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
	shfree(state->rooms);

	memset(state, 0, sizeof(*state));

	printf("%s '%s'\n", "Debug information has been logged to", log_path());

	log_mutex_destroy();
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

	pthread_mutex_lock(&state->queue_mutex);
	if ((queue_push_tail(&state->queue, item)) == -1) {
		queue_item_free(item);
		pthread_mutex_unlock(&state->queue_mutex);
		return -1;
	}
	pthread_cond_broadcast(&state->queue_cond);
	/* pthread_cond_wait in queue thread blocks until we unlock the mutex here
	 * before relocking it. */
	pthread_mutex_unlock(&state->queue_mutex);

	return 0;
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
	bool reset = room_reset_if_recalculate(room, &points);

	/* Ensure that new events are filled if we didn't reset. */
	if (!reset) {
		pthread_mutex_lock(&room->realloc_or_modify_mutex);
		room_fill_new_events(room, &points);
		pthread_mutex_unlock(&room->realloc_or_modify_mutex);
	}
}

static void
tab_room_reset_rooms(struct tab_room *tab_room, struct hm_room *rooms);

static enum widget_error
handle_tree(
  struct tab_room *tab_room, struct tb_event *event, struct hm_room *rooms) {
	assert(tab_room);
	assert(event);

	switch (event->key) {
	case TB_KEY_ENTER:
		/* Selected and not a root node (Invites/Spaces/DMs/Rooms/...) */
		if (tab_room->treeview.selected
			&& tab_room->treeview.selected->parent->parent) {
			tab_room->selected_room = tab_room->treeview.selected->data;

			if (tab_room->selected_room->value->info.is_space) {
				arrput(tab_room->path, tab_room->selected_room->key);
				tab_room->selected_room = NULL;

				/* Reset to the first room in the space. */
				tab_room_reset_rooms(tab_room, rooms);
			} else {
				reset_room_buffer(tab_room->selected_room->value);
			}

			return WIDGET_REDRAW;
		}

		break;
	case TB_KEY_ARROW_UP:
		return treeview_event(&tab_room->treeview, TREEVIEW_UP);
	case TB_KEY_ARROW_DOWN:
		return treeview_event(&tab_room->treeview, TREEVIEW_DOWN);
	default:
		if (event->ch == ' ') {
			return treeview_event(&tab_room->treeview, TREEVIEW_EXPAND);
		}

		break;
	}

	return WIDGET_NOOP;
}

static enum widget_error
handle_input(struct input *input, struct tb_event *event, bool *enter_pressed) {
	assert(input);
	assert(event);

	if (!event->key && event->ch) {
		return input_handle_event(input, INPUT_ADD, event->ch);
	}

	bool mod = (event->mod & TB_MOD_SHIFT) == TB_MOD_SHIFT;
	bool mod_enter = (event->mod & TB_MOD_ALT) == TB_MOD_ALT;

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

static enum widget_error
handle_tab_room(
  struct state *state, struct tab_room *tab_room, struct tb_event *event) {
	enum widget_error ret = WIDGET_NOOP;

	if (!tab_room->selected_room) {
		return ret;
	}

	struct room *room = tab_room->selected_room->value;

	if (event->type == TB_EVENT_RESIZE) {
		reset_room_buffer(room);
		return WIDGET_REDRAW;
	}

	if (event->type == TB_EVENT_MOUSE) {
		pthread_mutex_lock(&room->realloc_or_modify_mutex);
		ret = handle_message_buffer(&room->buffer, event);
		pthread_mutex_unlock(&room->realloc_or_modify_mutex);
	} else {
		bool enter_pressed = false;

		switch (tab_room->widget) {
		case TAB_ROOM_TREE:
			ret = handle_tree(tab_room, event, state->rooms);
			break;
		case TAB_ROOM_INPUT:
			ret = handle_input(&tab_room->input, event, &enter_pressed);

			if (enter_pressed) {
				char *buf = input_buf(&tab_room->input);

				/* Empty field. */
				if (!buf) {
					break;
				}

				struct sent_message *message = malloc(sizeof(*message));

				assert(tab_room->selected_room);

				*message = (struct sent_message) {
				  .has_reply = false,
				  .reply_index = 0,
				  .buf = buf,
				  .room_id = tab_room->selected_room->key,
				};

				lock_and_push(
				  state, queue_item_alloc(QUEUE_ITEM_MESSAGE, message));

				ret = input_handle_event(&tab_room->input, INPUT_CLEAR);
			}
			break;
		default:
			assert(0);
		}
	}

	return ret;
}

static void
draw_cb(void *data, struct widget_points *points, bool is_selected) {
	assert(data);
	assert(points);

	widget_print_str(points->x1, points->y1, points->x2,
	  is_selected ? TB_REVERSE : TB_DEFAULT, TB_DEFAULT, data);
}

static void
room_draw_cb(void *data, struct widget_points *points, bool is_selected) {
	assert(data);
	assert(points);

	struct room *room = ((struct hm_room *) data)->value;

	const char *str = room->info.name ? room->info.name : "Empty Room";
	widget_print_str(points->x1, points->y1, points->x2,
	  is_selected ? TB_REVERSE : TB_DEFAULT, TB_DEFAULT, str);
}

static void
tab_room_finish(struct tab_room *tab_room) {
	if (tab_room) {
		input_finish(&tab_room->input);
		treeview_node_finish(&tab_room->treeview.root);
		arrfree(tab_room->room_nodes);
		arrfree(tab_room->path);
		memset(tab_room, 0, sizeof(*tab_room));
	}
}

static int
tab_room_init(struct tab_room *tab_room) {
	assert(tab_room);

	*tab_room = (struct tab_room) {.widget = TAB_ROOM_TREE};

	int ret = input_init(&tab_room->input, TB_DEFAULT, false);
	assert(ret == 0);

	ret = treeview_init(&tab_room->treeview);
	assert(ret == 0);

	for (size_t i = 0; i < NODE_MAX; i++) {
		treeview_node_init(
		  &tab_room->root_nodes[i], noconst(root_node_str[i]), draw_cb);
		treeview_node_add_child(
		  &tab_room->treeview.root, &tab_room->root_nodes[i]);
	}

	tab_room->treeview.selected = tab_room->treeview.root.nodes[0];

	return 0;
}

static void
tab_room_add_room(
  struct tab_room *tab_room, size_t index, struct hm_room *room) {
	treeview_node_init(&tab_room->room_nodes[index], room, room_draw_cb);

	treeview_node_add_child(
	  &tab_room
		 ->root_nodes[room->value->info.is_space ? NODE_SPACES : NODE_ROOMS],
	  &tab_room->room_nodes[index]);

	if (tab_room->selected_room
		&& room->value == tab_room->selected_room->value) {
		enum widget_error ret = treeview_event(
		  &tab_room->treeview, TREEVIEW_JUMP, &tab_room->room_nodes[index]);
		assert(ret == WIDGET_REDRAW);
	}
}

/* We're lazy so we just reset the whole tree view on any space related change
 * such as the removal/addition of a room from/to a space. This is much less
 * error prone than manually managing the nodes, and isn't really that
 * inefficient if you consider how infrequently room changes occur. */
static void
tab_room_reset_rooms(struct tab_room *tab_room, struct hm_room *rooms) {
	assert(tab_room);

	tab_room->treeview.selected = NULL;

	for (size_t i = 0; i < NODE_MAX; i++) {
		arrsetlen(tab_room->treeview.root.nodes[i]->nodes, 0);
	}

	if (arrlenu(tab_room->path) > 0) {
		/* TODO verify path. */
		ptrdiff_t tmp = 0;
		struct room *space
		  = shget_ts(rooms, tab_room->path[arrlenu(tab_room->path) - 1], tmp);
		assert(space);

		/* A child space might have more rooms than root orphans. */
		arrsetlen(tab_room->room_nodes, shlenu(space->children));

		for (size_t i = 0, skipped = 0, len = shlenu(space->children); i < len;
			 i++) {
			ptrdiff_t child_index
			  = shgeti_ts(rooms, space->children[i].key, tmp);

			/* Child room not joined yet. */
			if (child_index == -1) {
				skipped++;
				continue;
			}

			tab_room_add_room(tab_room, i - skipped, &rooms[child_index]);
		}
	} else {
		arrsetlen(tab_room->room_nodes, shlenu(rooms));

		for (size_t i = 0, len = shlenu(rooms); i < len; i++) {
			tab_room_add_room(tab_room, i, &rooms[i]);
		}
	}

	if (tab_room->treeview.selected) {
		return;
	}

	/* Find the first non-empty node and choose it's first room as the
	 * selected one. */
	for (size_t i = 0; i < NODE_MAX; i++) {
		struct treeview_node **nodes = tab_room->treeview.root.nodes[i]->nodes;

		if (arrlenu(nodes) > 0) {
			enum widget_error ret
			  = treeview_event(&tab_room->treeview, TREEVIEW_JUMP, nodes[0]);
			assert(ret == WIDGET_REDRAW);

			tab_room->selected_room = tab_room->treeview.selected->data;
			return;
		}
	}

	tab_room->selected_room = NULL;
}

static bool
handle_accumulated_sync(struct hm_room **rooms, struct tab_room *tab_room,
  struct accumulated_sync_data *data) {
	assert(rooms);
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

		ptrdiff_t tmp = 0;
		if (!(shget_ts(*rooms, room->id, tmp))) {
			any_tree_changes = true; /* New room added */
			shput(*rooms, room->id, room->room);
		}

		if (tab_room->selected_room
			&& strcmp(room->id, tab_room->selected_room->key) == 0) {
			assert(room->room == tab_room->selected_room->value);
			any_room_events = true;
			reset_room_buffer(tab_room->selected_room->value);
		}
	}

	for (size_t i = 0, len = arrlenu(data->space_events); i < len; i++) {
		struct accumulated_space_event *event = &data->space_events[i];

		assert(event->parent);
		assert(event->child);

		switch (event->status) {
		case CACHE_DEFERRED_ADDED:
		case CACHE_DEFERRED_REMOVED:
			break;
		default:
			assert(0);
		}
	}

	if (any_tree_changes || arrlenu(data->space_events) > 0) {
		tab_room_reset_rooms(tab_room, *rooms);
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

	tab_room_reset_rooms(&tab_room, state->rooms);

	for (bool redraw = true;;) {
		if (redraw) {
			redraw = false;

			tb_clear();
			tb_hide_cursor();

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

			uintptr_t data = 0;

			ssize_t ret
			  = read(state->thread_comm_pipe[PIPE_READ], &data, sizeof(data));

			assert(ret == 0);
			assert(data);

			/* Ensure that we redraw if we had changes. */
			redraw = handle_accumulated_sync(
			  /* NOLINTNEXTLINE(performance-no-int-to-ptr) */
			  &state->rooms, &tab_room, (struct accumulated_sync_data *) data);

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

	ptrdiff_t tmp = 0;
	struct room *room = shget_ts(state->rooms, noconst(room_id), tmp);
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

	ptrdiff_t tmp = 0;
	struct room *room = shget_ts(state->rooms, noconst(room_id), tmp);
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

	/* Hashmap for finding orphaned rooms with no children. */
	struct hm_room *child_rooms = NULL;
	/* Don't call SHMAP_INIT() as we don't need to duplicate strings here. */

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
			shput(child_rooms, noconst(space.child_id), space_child);
		}

		/* No need to finish the children iterator since it only borrows a
		 * cursor from the parent iterator, which is freed when the parent is
		 * finished. */
	}

	for (size_t i = 0, len = shlenu(state->rooms); i < len; i++) {
		if ((shget(child_rooms, state->rooms[i].key))) {
			continue;
		}

		/* Orphan since the room/space hasn't been added to any space. */
		/* TODO shput(state->rooms, state->rooms[i].key, state->rooms[i].value);
		 */
	}

	shfree(child_rooms);
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

		ptrdiff_t tmp = 0;
		struct room *room = shget_ts(state->rooms, sync_room.id, tmp);

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
