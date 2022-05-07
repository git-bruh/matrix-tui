#ifndef STATE_H
#define STATE_H
/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "db/cache.h"
#include "util/queue.h"
#include "widgets.h"

#include <errno.h>
#include <unistd.h>

enum { THREAD_SYNC = 0, THREAD_QUEUE, THREAD_MAX };
enum { PIPE_READ = 0, PIPE_WRITE, PIPE_MAX };
enum { FD_TTY = 0, FD_RESIZE, FD_PIPE, FD_MAX };

enum {
	EVENTS_IN_TIMELINE = MATRIX_ROOM_MESSAGE | MATRIX_ROOM_ATTACHMENT,
	STATE_IN_TIMELINE
	= MATRIX_ROOM_MEMBER | MATRIX_ROOM_NAME | MATRIX_ROOM_TOPIC,
};

enum space_tree_root_nodes {
	NODE_INVITES = 0,
	NODE_SPACES,
	NODE_DMS,
	NODE_ROOMS,
	NODE_MAX
};

struct hm_room {
	char *key;
	struct room *value;
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
	struct hm_room *rooms;
	/* Orphaned rooms/spaces without a parent space. */
	struct hm_room *orphaned_rooms;
};

#define read safe_read
#define write safe_write
#endif /* !STATE_H */
