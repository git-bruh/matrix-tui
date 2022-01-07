#include "queue_callbacks.h"

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
		fprintf(stderr, "Failed to send message to room '%s': %s\n",
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
			code = (enum matrix_code) LOGIN_DB_FAIL;
		}
	}

	write(state->thread_comm_pipe[PIPE_WRITE], &code, sizeof(code));

	free(access_token);
}

const struct queue_callback queue_callbacks[QUEUE_ITEM_MAX] = {
  [QUEUE_ITEM_MESSAGE] = {handle_sent_message, free_sent_message},
  [QUEUE_ITEM_LOGIN] = {		handle_login,			  free},
};