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

static void
message_destroy(struct message *message) {
	if (message) {
		arrfree(message->body);
		free(message->sender);
		free(message);
	}
}

static struct message *
message_alloc(const char *body, const char *sender, uint32_t *username,
  uint64_t index, const uint64_t *index_reply, bool formatted) {
	assert(body);
	assert(sender);
	assert(username);

	struct message *message = malloc(sizeof(*message));

	if (message) {
		*message = (struct message) {.formatted = formatted,
		  .reply = !!index_reply,
		  .index = index,
		  .index_reply = (index_reply ? *index_reply : 0),
		  .username = username,
		  .body = buf_to_uint32_t(body, 0),
		  .sender = strdup(sender)};

		if (!message->body || !message->sender) {
			message_destroy(message);
			return NULL;
		}
	}

	return message;
}

struct message *
room_bsearch(struct room *room, uint64_t index) {
	if (!room) {
		return NULL;
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
		return NULL;
	}

	struct message **message = bsearch(
	  &index, timeline->buf, timeline->len, sizeof(*timeline->buf), cmp);

	if (!message) {
		return NULL;
	}

	assert(*message);
	return *message;
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

	/* We lock for every member here, but it's not a big
	 * issue since member events are very rare and we won't
	 * have more than 1-2 of them per-sync
	 * except for large syncs like the initial sync. */
	pthread_mutex_lock(&room->realloc_or_modify_mutex);

	if (sh_index < 0) {
		uint32_t **usernames = NULL;
		arrput(usernames, username_or_stripped_mxid);
		shput(room->members, mxid, usernames);
	} else {
		arrput(room->members[sh_index].value, username_or_stripped_mxid);
	}

	pthread_mutex_unlock(&room->realloc_or_modify_mutex);

	return 0;
}

static int
room_put_message_event(struct room *room, enum timeline_type timeline,
  uint64_t index, const struct matrix_timeline_event *event) {
	assert(room);
	assert(event->base.sender);
	assert(event->message.body);
	assert(event->type == MATRIX_ROOM_MESSAGE);
	assert(timeline < TIMELINE_MAX);
	assert(!(room_bsearch(room, index)));

	ptrdiff_t tmp = 0;
	uint32_t **usernames = shget_ts(room->members, event->base.sender, tmp);
	size_t usernames_len = arrlenu(usernames);

	assert(usernames_len);

	struct message *message = message_alloc(event->message.body,
	  event->base.sender, usernames[usernames_len - 1], index, NULL, false);

	if (!message) {
		return -1;
	}

	/* We only lock if the message buffer actually needs to
	 * grow. Otherwise, the reader thread has a length of the
	 * array which stops at the
	 * index where we're inserting the new messages, so no
	 * races. */
	LOCK_IF_GROW(room->timelines[timeline].buf, &room->realloc_or_modify_mutex);

	/* This is safe as the reader thread will have the old value
	 * of len stored and not access anything beyond that. */
	arrput(room->timelines[timeline].buf, message);

	size_t len = arrlenu(room->timelines[timeline].buf);

	if (len > 1) {
		/* Ensure correct order for bsearch. */
		switch (timeline) {
		case TIMELINE_FORWARD:
			assert(room->timelines[timeline].buf[len - 1]->index
				   > room->timelines[timeline].buf[len - 2]->index);
			break;
		case TIMELINE_BACKWARD:
			assert(room->timelines[timeline].buf[len - 1]->index
				   < room->timelines[timeline].buf[len - 2]->index);
			break;
		default:
			assert(0);
		}
	}

	room->timelines[timeline].len = len;

	return 0;
}

static int
room_redact_event(struct room *room, uint64_t index) {
	assert(room);

	struct message *to_redact = room_bsearch(room, index);

	if (!to_redact) {
		return -1;
	}

	pthread_mutex_lock(&room->realloc_or_modify_mutex);
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
room_put_event(struct room *room, const struct matrix_sync_event *event,
  bool backward, uint64_t index, uint64_t redaction_index) {
	assert(room);
	assert(event);

	bool redaction_valid_if_present = false;

	if (event->type == MATRIX_EVENT_STATE && !event->state.is_in_timeline) {
		assert(index == (uint64_t) -1);
	} else {
		assert(index != (uint64_t) -1);
	}

	switch (event->type) {
	case MATRIX_EVENT_EPHEMERAL:
		break;
	case MATRIX_EVENT_STATE:
		switch (event->state.type) {
		case MATRIX_ROOM_MEMBER:
			room_put_member(room, event->state.base.sender,
			  event->state.content.member.displayname);
		default:
			break;
		}
		break;
	case MATRIX_EVENT_TIMELINE:
		switch (event->timeline.type) {
		case MATRIX_ROOM_MESSAGE:
			room_put_message_event(room,
			  (backward ? TIMELINE_BACKWARD : TIMELINE_FORWARD), index,
			  &event->timeline);
			break;
		case MATRIX_ROOM_REDACTION:
			if (redaction_index != (uint64_t) -1) {
				room_redact_event(room, redaction_index);
				redaction_valid_if_present = true;
			}
			break;
		case MATRIX_ROOM_ATTACHMENT:
			break;
		default:
			assert(0);
		}
		break;
	default:
		assert(0);
	}

	if (redaction_index != (uint64_t) -1) {
		assert(redaction_valid_if_present);
	}

	return 0;
}

static void
timeline_finish(struct timeline *timeline) {
	if (timeline && timeline->buf) {
		for (size_t i = 0, len = timeline->len; i < len; i++) {
			message_destroy(timeline->buf[i]);
		}

		arrfree(timeline->buf);
		memset(timeline, 0, sizeof(*timeline));
	}
}

static int
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

bool
room_fill_old_events(struct room *room, struct widget_points *points) {
	assert(room);
	assert(points);

	struct message **buf = room->timelines[TIMELINE_BACKWARD].buf;
	size_t len = room->timelines[TIMELINE_BACKWARD].len;

	for (size_t i = len; i > 0; i--) {
		if (!buf[i - 1]->redacted) {
			message_buffer_insert(&room->buffer, points, buf[i - 1]);
		}
	}

	return (len > 0);
}

bool
room_fill_new_events(struct room *room, struct widget_points *points) {
	assert(room);
	assert(points);

	size_t original_consumed = room->already_consumed;

	struct message **buf = room->timelines[TIMELINE_FORWARD].buf;
	size_t len = room->timelines[TIMELINE_FORWARD].len;

	for (; room->already_consumed < len; room->already_consumed++) {
		if (!buf[room->already_consumed]->redacted) {
			message_buffer_insert(
			  &room->buffer, points, buf[room->already_consumed]);
		}
	}

	return (room->already_consumed > original_consumed);
}

bool
room_reset_if_recalculate(struct room *room, struct widget_points *points) {
	assert(room);
	assert(points);

	pthread_mutex_lock(&room->realloc_or_modify_mutex);
	bool recalculate = message_buffer_should_recalculate(&room->buffer, points);

	if (recalculate) {
		room->already_consumed = 0;
		message_buffer_zero(&room->buffer);
		room_fill_old_events(room, points);
		room_fill_new_events(room, points);
		message_buffer_ensure_sane_scroll(&room->buffer);
	}
	pthread_mutex_unlock(&room->realloc_or_modify_mutex);

	return recalculate;
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
