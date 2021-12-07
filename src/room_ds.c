/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "room_ds.h"

#include "stb_ds.h"

#include <assert.h>
#include <stdlib.h>

static int
message_cmp_forward(const void *key, const void *array_item) {
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
message_cmp_backward(const void *key, const void *array_item) {
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

static int
find_room_timeline(struct room *room, uint64_t index, size_t *timeline_index,
  size_t *buf_index) {
	assert(room);
	assert(timeline_index);
	assert(buf_index);

	bool found = false;

	*timeline_index = TIMELINE_FORWARD;

	for (size_t i = 0, len = arrlenu(room->timelines[*timeline_index]); i < len;
		 i++) {
		if (room->timelines[*timeline_index][i].buf[0].index <= index) {
			*buf_index = i;
			found = true;
		} else {
			break;
		}
	}

	if (found) {
		return 0;
	}

	*timeline_index = TIMELINE_BACKWARD;

	for (size_t i = 0, len = arrlenu(room->timelines[*timeline_index]); i < len;
		 i++) {
		/* Reverse order. */
		if (room->timelines[*timeline_index][i].buf[0].index >= index) {
			*buf_index = i;
			found = true;
		} else {
			break;
		}
	}

	if (found) {
		return 0;
	}

	return -1;
}

int
room_bsearch(struct room *room, uint64_t index, struct room_index *out_index) {
	if (!room) {
		return -1;
	}

	size_t timeline_index = 0;
	size_t buf_index = 0;

	if ((find_room_timeline(room, index, &timeline_index, &buf_index)) != 0) {
		return -1;
	}

	struct timeline *timeline = &room->timelines[timeline_index][buf_index];

	struct message *message = NULL;

	switch (timeline_index) {
	case TIMELINE_FORWARD:
		message = bsearch(&index, timeline->buf, timeline->len,
		  sizeof(*timeline->buf), message_cmp_forward);
		break;
	case TIMELINE_BACKWARD:
		message = bsearch(&index, timeline->buf, timeline->len,
		  sizeof(*timeline->buf), message_cmp_backward);
		break;
	default:
		assert(0);
	}

	if (!message) {
		return -1;
	}

	*out_index = (struct room_index) {
	  .index_timeline = timeline_index,
	  .index_buf = buf_index,
	  .index_message = (size_t) (message - timeline->buf),
	};

	assert(out_index->index_message < timeline->len);

	return 0;
}

int
timeline_init(struct timeline *timeline) {
	if (timeline) {
		*timeline = (struct timeline) {
		  .buf = calloc(TIMELINE_BUFSZ, sizeof(*timeline->buf))};

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

		free(timeline->buf);
		memset(timeline, 0, sizeof(*timeline));
	}
}

struct room *
room_alloc(struct room_info *info) {
	struct room *room = malloc(sizeof(*room));

	if (room) {
		*room = (struct room) {
		  .info = info,
		  .nontrivial_modification_mutex = PTHREAD_MUTEX_INITIALIZER,
		};

		const size_t initial_buffers = 2;

		for (size_t i = 0; i < TIMELINE_MAX; i++) {
			arrsetcap(room->timelines[i], initial_buffers);

			if (!room->timelines[i]) {
				room_destroy(room);
				return NULL;
			}
		}

		struct timeline timeline = {0};

		if ((timeline_init(&timeline)) == 0) {
			arrput(room->timelines[TIMELINE_FORWARD], timeline);
			return room;
		}

		room_destroy(room);
	}

	return NULL;
}

void
room_destroy(struct room *room) {
	if (room) {
		for (size_t i = 0; i < TIMELINE_MAX; i++) {
			for (size_t j = 0, len = arrlenu(room->timelines[i]); j < len;
				 j++) {
				timeline_finish(&room->timelines[i][j]);
			}

			arrfree(room->timelines[i]);
		}

		room_info_destroy(room->info);
		free(room);
	}
}
