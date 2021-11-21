#include <stddef.h>

enum { QUEUE_SIZE = 50 };

struct queue {
	size_t head;
	size_t tail;
	void *data[QUEUE_SIZE];
};

int
queue_push_tail(struct queue *queue, void *data);
void *
queue_pop_head(struct queue *queue);
