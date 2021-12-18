#ifndef ROOM_DS_H
#define ROOM_DS_H
/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "cache.h"
#include "message_buffer.h"
#include "stb_ds.h"

#include <pthread.h>
#include <stdatomic.h>

#define lock_if_grow(arr, mutex)                                               \
	do {                                                                       \
		if (!((stbds_header(arr)->length + 1)                                  \
			  > stbds_header(arr)->capacity)) {                                \
			break;                                                             \
		}                                                                      \
		pthread_mutex_lock(mutex);                                             \
		stbds_arrgrow(arr, 1, 0);                                              \
		pthread_mutex_unlock(mutex);                                           \
	} while (0)

enum {
	TIMELINE_FORWARD = 0, /* New messages. */
	TIMELINE_BACKWARD,	  /* Backfilled messages. */
	TIMELINE_MAX
};
enum { TIMELINE_INITIAL_RESERVE = 50 };

struct message {
	bool edited;
	bool formatted;
	bool redacted;
	bool reply;
	uint64_t index; /* Index from database. */
	uint64_t
	  index_reply; /* Index (from database) of the message being replied to. */
	char *body;	   /* HTML, if formatted is true. */
	char *sender;
};

struct timeline {
	/* Pointer to heap-allocated structures so that we can pass
	 * them between threads. */
	struct message **buf;
	/* Index of the last inserted message, must be an atomic since we store it
	 * once in the reader thread and later re-assign it from the writer after
	 * writing events. This allows the reader thread to read old events while
	 * the writer inserts after the reader's index. */
	_Atomic size_t len;
};

struct room {
	struct room_info *info;
	/* Rendered message indices. */
	struct message_buffer buffer;
	/* .buf MUST have an initial capacity set with arrsetcap. Binary search is
	 * used to find messages in the appropriate timeline. */
	struct timeline timelines[TIMELINE_MAX];
	/* Locked by reader for the whole duration of an iteration. Used to realloc
	 * the message buffer or mark existing messages as edited/redacetd. */
	pthread_mutex_t realloc_or_modify_mutex;
};

struct room_index {
	size_t index_timeline;
	size_t index_buf;
};

struct message *
message_alloc(char *body, char *sender, uint64_t index,
  const uint64_t *index_reply, bool formatted);
void
message_destroy(struct message *message);
int
room_bsearch(struct room *room, uint64_t index, struct room_index *out_index);
int
timeline_init(struct timeline *timeline);
void
timeline_finish(struct timeline *timeline);
struct room *
room_alloc(struct room_info *info);
void
room_destroy(struct room *room);
#endif /* !ROOM_DS_H */
