#ifndef ROOM_DS_H
#define ROOM_DS_H
/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "cache.h"
#include "message_buffer.h"
#include "stb_ds.h"
#include "ui.h"

#include <pthread.h>
#include <stdatomic.h>

#define LOCK_IF_GROW(arr, mutex)                                               \
	do {                                                                       \
		/* stbds_header expectes nonnull */                                    \
		if ((arr)                                                              \
			&& !((stbds_header(arr)->length + 1)                               \
				 > stbds_header(arr)->capacity)) {                             \
			break;                                                             \
		}                                                                      \
		pthread_mutex_lock(mutex);                                             \
		stbds_arrgrow(arr, 1, 0);                                              \
		pthread_mutex_unlock(mutex);                                           \
	} while (0)

/* stb_ds doesn't duplicate strings by default.
 * We rarely delete from the hashmaps so we use an arena allocator. */
#define SHMAP_INIT(map) sh_new_strdup(map)

enum timeline_type {
	TIMELINE_FORWARD = 0, /* New messages. */
	TIMELINE_BACKWARD,	  /* Backfilled messages iterated in reverse order. */
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
	/* Pointer to username at the current index from hashmap. */
	uint32_t *username;
	uint32_t *body; /* HTML, if formatted is true. */
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
	size_t already_consumed; /* No. of items consumed from timeline. */
	struct members_map *members;
	/* If the room is a space. children[i].value is always true as we just use
	 * this as a set, not hashmap. */
	struct {
		char *key;
		bool value;
	} * children;
	struct room_info info;
	/* Rendered message indices. */
	struct message_buffer buffer;
	/* .buf MUST have an initial capacity set with arrsetcap. Binary search is
	 * used to find messages in the appropriate timeline. */
	struct timeline timelines[TIMELINE_MAX];
	/* Locked by reader for the whole duration of an iteration. Used to realloc
	 * the message buffer or mark existing messages as edited/redacetd. */
	pthread_mutex_t realloc_or_modify_mutex;
};

struct message *
room_bsearch(struct room *room, uint64_t index);
void
room_add_child(struct room *room, char *child);
void
room_remove_child(struct room *room, char *child);
int
room_put_member(struct room *room, char *mxid, char *username);
int
room_put_event(struct room *room, const struct matrix_sync_event *event,
  bool backward, uint64_t index, uint64_t redaction_index);
bool
room_fill_old_events(struct room *room, struct widget_points *points);
bool
room_fill_new_events(struct room *room, struct widget_points *points);
bool
room_reset_if_recalculate(struct room *room, struct widget_points *points);
struct room *
room_alloc(struct room_info info);
void
room_destroy(struct room *room);
#endif /* !ROOM_DS_H */
