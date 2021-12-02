#include "matrix-priv.h"

struct ll *
matrix_ll_alloc(void (*free)(void *data)) {
	struct ll *ll = calloc(1, sizeof(*ll));

	if (!ll) {
		return NULL;
	}

	ll->free = free;

	return ll;
}

struct node *
matrix_ll_append(struct ll *ll, void *data) {
	assert(ll);

	struct node *node = calloc(1, sizeof(*node));

	if (!node) {
		return NULL;
	}

	node->data = data;

	if (ll->tail) {
		node->prev = ll->tail;
		ll->tail->next = node;
		ll->tail = node;
	} else {
		ll->tail = node;
		node->prev = node->next = NULL;
	}

	return node;
}

void
matrix_ll_remove(struct ll *ll, struct node *node) {
	assert(ll);

	if (!node) {
		return;
	}

	if (node->prev) {
		node->prev->next = node->next;
	}

	node->next ? (node->next->prev = node->prev) : (ll->tail = node->prev);

	if (ll->free) {
		ll->free(node->data);
	}

	free(node);
}

void
matrix_ll_free(struct ll *ll) {
	if (!ll) {
		return;
	}

	struct node *prev = NULL;

	while (ll->tail) {
		prev = ll->tail->prev;

		if (ll->free) {
			ll->free(ll->tail->data);
		}

		free(ll->tail);

		ll->tail = prev;
	}

	free(ll);
}
