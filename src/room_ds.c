/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "room_ds.h"

#include "message_buffer.h"
#include "stb_ds.h"
#include "ui.h"

#include <assert.h>
#include <stdlib.h>

void
node_draw_cb(void *data, struct widget_points *points, bool is_selected);

static int
cmp_forward(const void *key, const void *array_item) {
	uint64_t i1 = *((const uint64_t *) key);
	uint64_t i2 = (*((struct message *const *) array_item))->index;

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
	uint64_t i2 = (*((struct message *const *) array_item))->index;

	if (i1 > i2) {
		return -1;
	}

	if (i1 < i2) {
		return 1;
	}

	return 0; /* Equal. */
}

struct message *
message_alloc(const char *body, const char *sender, size_t index_username,
  uint64_t index, const uint64_t *index_reply, bool formatted) {
	struct message *message = malloc(sizeof(*message));

	if (message) {
		*message = (struct message) {.formatted = formatted,
		  .reply = !!index_reply,
		  .index = index,
		  .index_reply = (index_reply ? *index_reply : 0),
		  .index_username = index_username,
		  .body = buf_to_uint32_t(body, 0),
		  .sender = strdup(sender)};

		if (!message->body || !message->sender) {
			message_destroy(message);
			return NULL;
		}
	}

	return message;
}

void
message_destroy(struct message *message) {
	if (message) {
		arrfree(message->body);
		free(message->sender);
		free(message);
	}
}

int
room_bsearch(struct room *room, uint64_t index, struct room_index *out_index) {
	if (!room || !out_index) {
		return -1;
	}

	struct timeline *timeline = NULL;
	int (*cmp)(const void *, const void *) = NULL;

	if (room->timelines[TIMELINE_FORWARD].len > 0
		&& room->timelines[TIMELINE_FORWARD].buf[0]->index <= index) {
		timeline = &room->timelines[TIMELINE_FORWARD];
		cmp = cmp_forward;
	} else if (room->timelines[TIMELINE_BACKWARD].len > 0
			   && room->timelines[TIMELINE_BACKWARD].buf[0]->index >= index) {
		/* Reverse search. */
		timeline = &room->timelines[TIMELINE_BACKWARD];
		cmp = cmp_backward;
	} else {
		return -1;
	}

	struct message **message = NULL;

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
room_put_member(struct room *room, char *mxid, char *username) {
	assert(room);
	assert(mxid);

	/* If len < 1 then displayname has been removed. */
	uint32_t *username_or_stripped_mxid
	  = (username && (strnlen(username, 1)) > 0) ? buf_to_uint32_t(username, 0)
												 : mxid_to_uint32_t(mxid);

	assert(username_or_stripped_mxid);

	ptrdiff_t tmp = 0;
	ptrdiff_t sh_index = shgeti_ts(room->members, mxid, tmp);

	if (sh_index < 0) {
		uint32_t **usernames = NULL;
		arrput(usernames, username_or_stripped_mxid);
		shput(room->members, mxid, usernames);
	} else {
		arrput(room->members[sh_index].value, username_or_stripped_mxid);
	}

	return 0;
}

int
room_put_message(
  struct room *room, struct timeline *timeline, struct message *message) {
	assert(room);
	assert(timeline == &room->timelines[TIMELINE_FORWARD]
		   || timeline == &room->timelines[TIMELINE_BACKWARD]);

	/* This is safe as the reader thread will have the old value
	 * of len stored and not access anything beyond that. */
	arrput(timeline->buf, message);

	size_t len = arrlenu(timeline->buf);

	if (len > 1) {
		/* Ensure correct order for bsearch. */
		if (timeline == &room->timelines[TIMELINE_FORWARD]) {
			assert(
			  timeline->buf[len - 1]->index > timeline->buf[len - 2]->index);
		} else {
			assert(
			  timeline->buf[len - 1]->index < timeline->buf[len - 2]->index);
		}
	}

	timeline->len = len;

	return 0;
}

int
room_put_message_event(struct room *room, struct timeline *timeline,
  uint64_t index, struct matrix_timeline_event *event) {
	assert(room);
	assert(event->base.sender);
	assert(event->message.body);
	assert(event->type == MATRIX_ROOM_MESSAGE);
	assert(timeline == &room->timelines[TIMELINE_FORWARD]
		   || timeline == &room->timelines[TIMELINE_BACKWARD]);

	ptrdiff_t tmp = 0;
	uint32_t **usernames = shget_ts(room->members, event->base.sender, tmp);
	size_t usernames_len = arrlenu(usernames);

	assert(usernames_len);

	struct message *message = message_alloc(event->message.body,
	  event->base.sender, usernames_len - 1, index, NULL, false);

	if (!message) {
		return -1;
	}

	room_put_message(room, timeline, message);

	return 0;
}

int
room_redact_event(struct room *room, uint64_t index) {
	assert(room);

	struct room_index out_index = {0};

	if ((room_bsearch(room, index, &out_index)) == -1) {
		return -1;
	}

	pthread_mutex_lock(&room->realloc_or_modify_mutex);
	struct message *to_redact
	  = room->timelines[out_index.index_timeline].buf[out_index.index_buf];
	assert(!to_redact->redacted); /* Can't redact something we already did. */
	assert(to_redact->body);
	to_redact->redacted = true;
	arrfree(to_redact->body);
	to_redact->body = NULL;
	message_buffer_redact(&room->buffer, index);
	pthread_mutex_unlock(&room->realloc_or_modify_mutex);

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
			message_destroy(timeline->buf[i]);
		}

		arrfree(timeline->buf);
		memset(timeline, 0, sizeof(*timeline));
	}
}

struct room *
room_alloc(void) {
	struct room *room = malloc(sizeof(*room));

	if (room) {
		*room = (struct room) {
		  .realloc_or_modify_mutex = PTHREAD_MUTEX_INITIALIZER,
		};

		int ret
		  = treeview_node_init(&room->tree_node, room, node_draw_cb, NULL);
		assert(ret == 0);

		SHMAP_INIT(room->members);

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

		for (size_t i = 0, len = shlenu(room->members); i < len; i++) {
			for (size_t j = 0, usernames_len = arrlenu(room->members[i].value);
				 j < usernames_len; j++) {
				arrfree(room->members[i].value[j]);
			}
			arrfree(room->members[i].value);
		}
		shfree(room->members);
		arrfree(room->spaces);
		arrfree(room->dms);
		arrfree(room->rooms);
		message_buffer_finish(&room->buffer);
		cache_room_info_finish(&room->info);
		free(room);
	}
}
