/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "app/state.h"

#include "util/log.h"

#include <assert.h>

void
state_reset_orphans(struct state_rooms *state_rooms) {
	assert(state_rooms);

	shfree(state_rooms->orphaned_rooms);

	struct {
		char *key;
		bool value;
	} *children = NULL;

	/* Collect a hashmap of all rooms that are a child of any other room. */
	for (size_t i = 0, len = shlenu(state_rooms->rooms); i < len; i++) {
		struct room *room = state_rooms->rooms[i].value;

		for (size_t j = 0, children_len = shlenu(room->children);
			 j < children_len; j++) {
			shput(children, room->children[j].key, true);
		}
	}

	/* Now any rooms which aren't children of any other room are orphans. */
	for (size_t i = 0, len = shlenu(state_rooms->rooms); i < len; i++) {
		if (shget(children, state_rooms->rooms[i].key)) {
			continue; /* A child */
		}

		shput(state_rooms->orphaned_rooms, state_rooms->rooms[i].key,
		  state_rooms->rooms[i].value);
	}

	shfree(children);
}

bool
handle_accumulated_sync(struct state_rooms *state_rooms,
  struct tab_room *tab_room, struct accumulated_sync_data *data) {
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

		if (!(rooms_get_room(state_rooms->rooms, room->id))) {
			any_tree_changes = true; /* New room added */
			/* This doesn't need locking as the syncer thread waits until
			 * we use all the accumulated data (this function). */
			shput(state_rooms->rooms, room->id, room->room);
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
		  = rooms_get_room(state_rooms->rooms, noconst(event->parent));
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
		state_reset_orphans(state_rooms);
		tab_room_reset_rooms(tab_room, state_rooms);

		return true;
	}

	return any_tree_changes || any_room_events;
}

static int
populate_room_users(struct state *state, const char *room_id) {
	assert(state);
	assert(room_id);

	struct cache_iterator iterator = {0};

	struct room *room
	  = rooms_get_room(state->state_rooms.rooms, noconst(room_id));
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

	struct room *room
	  = rooms_get_room(state->state_rooms.rooms, noconst(room_id));
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

int
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

		shput(state->state_rooms.rooms, noconst(id), room);
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

		struct room *space_room
		  = shget(state->state_rooms.rooms, noconst(space.id));

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
			  = shget(state->state_rooms.rooms, noconst(space.child_id));

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

	state_reset_orphans(&state->state_rooms);

	return 0;
}

void
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

		struct room *room
		  = rooms_get_room(state->state_rooms.rooms, sync_room.id);

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

	safe_write(state->thread_comm_pipe[PIPE_WRITE], &ptr, sizeof(ptr));

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
