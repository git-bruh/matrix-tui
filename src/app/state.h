#pragma once
/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "app/hm_room.h"
#include "app/queue_callbacks.h"
#include "db/cache.h"
#include "util/queue.h"
#include "widgets.h"

#include <errno.h>
#include <unistd.h>

enum { THREAD_SYNC = 0, THREAD_QUEUE, THREAD_MAX };
enum { PIPE_READ = 0, PIPE_WRITE, PIPE_MAX };

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
