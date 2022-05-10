/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "app/queue_callbacks.h"

#include "app/state.h"
#include "util/log.h"

#include <assert.h>
#include <stdio.h>

void
queue_item_free(struct queue_item *item) {
	if (item) {
		queue_callbacks[item->type].free(item->data);
		free(item);
	}
}

struct queue_item *
queue_item_alloc(enum queue_item_type type, void *data) {
	assert(type < QUEUE_ITEM_MAX);

	struct queue_item *item
	  = (type < QUEUE_ITEM_MAX && data) ? malloc(sizeof(*item)) : NULL;

	if (item) {
		*item = (struct queue_item) {
		  .type = type,
		  .data = data,
		};
	} else {
		queue_callbacks[type].free(data);
	}

	return item;
}

int
lock_and_push(struct state *state, struct queue_item *item) {
	if (!item) {
		return -1;
	}

	pthread_mutex_lock(&state->queue_mutex);
	if ((queue_push_tail(&state->queue, item)) == -1) {
		queue_item_free(item);
		pthread_mutex_unlock(&state->queue_mutex);
		return -1;
	}
	pthread_cond_broadcast(&state->queue_cond);
	/* pthread_cond_wait in queue thread blocks until we unlock the mutex here
	 * before relocking it. */
	pthread_mutex_unlock(&state->queue_mutex);

	return 0;
}

static void
free_sent_message(void *data) {
	struct sent_message *message = data;

	if (message) {
		free(message->buf);
		free(message);
	}
}

static void
handle_sent_message(struct state *state, void *data) {
	assert(state);
	assert(data);

	struct sent_message *sent_message = data;
	assert(sent_message->buf);
	assert(sent_message->room_id);

	char *event_id = NULL;

	enum matrix_code code = matrix_send_message(state->matrix, &event_id,
	  sent_message->room_id, "m.text", sent_message->buf, NULL);

	if (code != MATRIX_SUCCESS) {
		LOG(LOG_WARN, "Failed to send message to room '%s': %s",
		  sent_message->room_id, matrix_strerror(code));
	}

	free(event_id);
}

static void
handle_login(struct state *state, void *data) {
	assert(state);
	assert(data);

	char *password = data;
	char *access_token = NULL;

	enum matrix_code code
	  = matrix_login(state->matrix, password, NULL, NULL, &access_token);

	if (code == MATRIX_SUCCESS) {
		assert(access_token);

		char *mxid = NULL;
		char *homeserver = NULL;

		matrix_get_mxid_homeserver(state->matrix, &mxid, &homeserver);

		assert(mxid);
		assert(homeserver);

		if ((cache_auth_set(&state->cache, DB_KEY_ACCESS_TOKEN, access_token))
			  != 0
			|| (cache_auth_set(&state->cache, DB_KEY_MXID, mxid)) != 0
			|| (cache_auth_set(&state->cache, DB_KEY_HOMESERVER, homeserver))
				 != 0) {
			/* Should've been caught in cache.c wrappers. */
			assert(0);
		}
	}

	write(state->thread_comm_pipe[PIPE_WRITE], &code, sizeof(code));

	free(access_token);
}

const struct queue_callback queue_callbacks[QUEUE_ITEM_MAX] = {
  [QUEUE_ITEM_MESSAGE] = {handle_sent_message, free_sent_message},
  [QUEUE_ITEM_LOGIN] = {		handle_login,			  free},
};
