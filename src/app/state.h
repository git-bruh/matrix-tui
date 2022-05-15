#pragma once
/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "app/hm_room.h"
#include "app/queue_callbacks.h"
#include "db/cache.h"
#include "ui/login_form.h"
#include "ui/tab_room.h"
#include "util/queue.h"
#include "widgets.h"

enum { THREAD_SYNC = 0, THREAD_QUEUE, THREAD_MAX };
enum { PIPE_READ = 0, PIPE_WRITE, PIPE_MAX };

enum {
	EVENTS_IN_TIMELINE = MATRIX_ROOM_MESSAGE | MATRIX_ROOM_ATTACHMENT,
	STATE_IN_TIMELINE
	= MATRIX_ROOM_MEMBER | MATRIX_ROOM_NAME | MATRIX_ROOM_TOPIC
};

struct state {
	_Atomic bool done;
	/* Pass data between the syncer thread and the UI thread. This exists as the
	 * UI thread has to be non-blocking and has to poll for events from the
	 * terminal along with matrix sync events. */
	int thread_comm_pipe[PIPE_MAX];
	pthread_t threads[THREAD_MAX];
	/* TODO array */
	_Atomic bool sync_cond_signaled;
	pthread_cond_t sync_cond;
	pthread_mutex_t sync_mutex;
	pthread_cond_t queue_cond;
	pthread_mutex_t queue_mutex;
	struct cache cache;
	struct queue queue;
	struct matrix *matrix;
	struct state_rooms state_rooms;
};

struct queue_item;

ssize_t
safe_read(int fildes, void *buf, size_t nbyte);
ssize_t
safe_write(int fildes, const void *buf, size_t nbyte);

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

/* TODO make everything take individual pointers instead of full struct state.
 */

enum widget_error
handle_tab_room(
  struct state *state, struct tab_room *tab_room, struct tb_event *event);
enum widget_error
handle_tab_login(
  struct state *state, struct tab_login *login, struct tb_event *event);

void
state_reset_orphans(struct state_rooms *state_rooms);
bool
handle_accumulated_sync(struct state_rooms *state_rooms,
  struct tab_room *tab_room, struct accumulated_sync_data *data);
int
populate_from_cache(struct state *state);
void
sync_cb(struct matrix *matrix, struct matrix_sync_response *response);
