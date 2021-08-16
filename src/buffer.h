#ifndef BUFFER_H
#define BUFFER_H
#include <stdint.h>
#include <sys/types.h>

struct buffer {
	uint32_t *buf;
	size_t cur, len;
};

struct buffer *buffer_alloc(void);
void buffer_free(struct buffer *buffer);

/* These functions return 0 on success, -1 on failure. */
int buffer_add(struct buffer *buffer, uint32_t uc);
int buffer_left(struct buffer *buffer);
int buffer_left_word(struct buffer *buffer);
int buffer_right(struct buffer *buffer);
int buffer_right_word(struct buffer *buffer);
int buffer_delete(struct buffer *buffer);
int buffer_delete_word(struct buffer *buffer);
#endif /* !BUFFER_H */
