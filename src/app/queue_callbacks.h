#pragma once
/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdbool.h>
#include <stdint.h>

struct state;

struct sent_message {
	bool has_reply;		  /* This exists as reply can be <= UINT64_MAX. */
	uint64_t reply_index; /* Reply index from DB. */
	char *buf;			  /* Markdown. */
	const char *room_id;  /* Current room's ID. */
};

struct queue_item {
	enum queue_item_type {
		QUEUE_ITEM_MESSAGE = 0,
		QUEUE_ITEM_LOGIN,
		QUEUE_ITEM_MAX
	} type;
	void *data;
};

struct queue_callback {
	void (*cb)(struct state *, void *);
	void (*free)(void *);
};

extern const struct queue_callback queue_callbacks[QUEUE_ITEM_MAX];

void
queue_item_free(struct queue_item *item);
struct queue_item *
queue_item_alloc(enum queue_item_type type, void *data);
int
lock_and_push(struct state *state, struct queue_item *item);
