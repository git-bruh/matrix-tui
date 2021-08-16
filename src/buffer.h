#include <stdint.h>
#include <sys/types.h>

struct buffer {
	uint32_t *buf;
	size_t cur, len;
};

struct buffer *buffer_alloc(void);
void buffer_free(struct buffer *buffer);
int buffer_add(struct buffer *buffer, uint32_t uc);
int buffer_left(struct buffer *buffer);
int buffer_left_word(struct buffer *buffer);
int buffer_right(struct buffer *buffer);
int buffer_right_word(struct buffer *buffer);
int buffer_delete(struct buffer *buffer);
int buffer_delete_word(struct buffer *buffer);
