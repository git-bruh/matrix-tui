#include "cache.h"
#include "stb_ds.h"

#include <pthread.h>
#include <stdatomic.h>

#define lock_if_grow(arr, mutex)                                               \
	do {                                                                       \
		if (!((stbds_header(arr)->length + 1)                                  \
			  > stbds_header(arr)->capacity)) {                                \
			continue;                                                          \
		}                                                                      \
		pthread_mutex_lock(mutex);                                             \
		stbds_arrgrow(arr, 1, 0);                                              \
		pthread_mutex_unlock(mutex);                                           \
	} while (0)

enum { TIMELINE_FORWARD = 0, TIMELINE_BACKWARD, TIMELINE_MAX };
enum { TIMELINE_INITIAL_RESERVE = 50 };

struct message {
	bool edited;
	bool formatted;
	bool redacted;
	bool reply;
	uint64_t index; /* Index from database. */
	uint64_t
	  index_reply; /* Index (from database) of the message being replied to. */
	char *body;	   /* HTML, if formatted is true. */
	char *sender;
};

struct timeline {
	struct message *buf;
	/* Index of the last inserted message, must be an atomic since we store it
	 * once in the reader thread and later re-assign it from the writer after
	 * writing events. This allows the reader thread to read old events while
	 * the writer inserts after the reader's index. */
	_Atomic size_t len;
};

struct room {
	uint64_t current_message_index;
	struct room_info *info;
	/* .buf MUST have an initial capacity set with arrsetcap. Binary search is
	 * used to find messages in the appropriate timeline. */
	struct timeline timelines[TIMELINE_MAX];
	/* Locked by reader for the whole duration of an iteration. Used to realloc
	 * the message buffer or mark existing messages as edited/redacetd. */
	pthread_mutex_t realloc_or_modify_mutex;
};

struct room_index {
	size_t index_timeline;
	size_t index_buf;
};

int
room_bsearch(struct room *room, uint64_t index, struct room_index *out_index);
int
timeline_init(struct timeline *timeline);
void
timeline_finish(struct timeline *timeline);
struct room *
room_alloc(struct room_info *info);
void
room_destroy(struct room *room);
