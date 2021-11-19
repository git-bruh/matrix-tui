/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

enum { QUEUE_SIZE = 10 };

/* Fixed-size FIFO */
struct queue {
	size_t head;
	size_t tail;
	void *data[QUEUE_SIZE];
};

int
queue_push_tail(struct queue *queue, void *data) {
	if (!queue || !data) {
		return -1;
	}

	if (queue->tail == QUEUE_SIZE) {
		queue->tail = 0;
	}

	assert(queue->tail < QUEUE_SIZE);

	/* We cycled back to the start but the data has still not been
	 * consumed by queue_pop_head(). */
	if (!!queue->data[queue->tail]) {
		return -1;
	}

	queue->data[queue->tail++] = data;

	return 0;
}

void *
queue_pop_head(struct queue *queue) {
	if (!queue) {
		return NULL;
	}

	if (queue->head == QUEUE_SIZE) {
		queue->head = 0;
	}

	assert(queue->head < QUEUE_SIZE);

	/* Empty queue. */
	if (!queue->data[queue->head]) {
		return NULL;
	}

	void *data = queue->data[queue->head];
	queue->data[queue->head++] = NULL;

	assert(data);

	return data;
}
