/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "room_ds.h"

#include "stb_ds.h"

#include <assert.h>
#include <stdlib.h>

static int
cmp_forward(const void *key, const void *array_item) {
	uint64_t i1 = *((const uint64_t *) key);
	uint64_t i2 = ((const struct message *) array_item)->index;

	if (i1 > i2) {
		return 1;
	}

	if (i1 < i2) {
		return -1;
	}

	return 0; /* Equal. */
}

/* Search in an array sorted in descending order. */
static int
cmp_backward(const void *key, const void *array_item) {
	uint64_t i1 = *((const uint64_t *) key);
	uint64_t i2 = ((const struct message *) array_item)->index;

	if (i1 > i2) {
		return -1;
	}

	if (i1 < i2) {
		return 1;
	}

	return 0; /* Equal. */
}

int
room_bsearch(struct room *room, uint64_t index, struct room_index *out_index) {
	if (!room || !out_index) {
		return -1;
	}

	struct timeline *timeline = NULL;
	int (*cmp)(const void *, const void *) = NULL;

	if (room->timelines[TIMELINE_FORWARD].buf[0].index <= index) {
		timeline = &room->timelines[TIMELINE_FORWARD];
		cmp = cmp_forward;
	} else if (room->timelines[TIMELINE_BACKWARD].buf[0].index >= index) {
		/* Reverse search. */
		timeline = &room->timelines[TIMELINE_BACKWARD];
		cmp = cmp_backward;
	} else {
		return -1;
	}

	struct message *message = NULL;

	if (!(message = bsearch(&index, timeline->buf, timeline->len,
			sizeof(*timeline->buf), cmp))) {
		return -1;
	}

	*out_index = (struct room_index) {
	  .index_timeline = (size_t) (timeline - room->timelines),
	  .index_buf = (size_t) (message - timeline->buf),
	};

	assert(out_index->index_timeline < TIMELINE_MAX);
	assert(out_index->index_buf < timeline->len);

	return 0;
}

int
timeline_init(struct timeline *timeline) {
	if (timeline) {
		*timeline = (struct timeline) {0};

		arrsetcap(timeline->buf, TIMELINE_INITIAL_RESERVE);

		if (timeline->buf) {
			return 0;
		}

		timeline_finish(timeline);
	}

	return -1;
}

void
timeline_finish(struct timeline *timeline) {
	if (timeline && timeline->buf) {
		for (size_t i = 0, len = timeline->len; i < len; i++) {
			free(timeline->buf[i].body);
			free(timeline->buf[i].sender);
		}

		arrfree(timeline->buf);
		memset(timeline, 0, sizeof(*timeline));
	}
}

struct room *
room_alloc(struct room_info *info) {
	struct room *room = malloc(sizeof(*room));

	if (room) {
		*room = (struct room) {
		  .info = info,
		  .realloc_or_modify_mutex = PTHREAD_MUTEX_INITIALIZER,
		};

		for (size_t i = 0; i < TIMELINE_MAX; i++) {
			if ((timeline_init(&room->timelines[i])) == -1) {
				room_destroy(room);
				return NULL;
			}
		}

		return room;
	}

	return NULL;
}

void
room_destroy(struct room *room) {
	if (room) {
		for (size_t i = 0; i < TIMELINE_MAX; i++) {
			timeline_finish(&room->timelines[i]);
		}

		room_info_destroy(room->info);
		free(room);
	}
}
