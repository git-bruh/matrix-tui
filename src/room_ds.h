#include "cache.h"

#include <pthread.h>
#include <stdatomic.h>

enum { TIMELINE_FORWARD = 0, TIMELINE_BACKWARD, TIMELINE_MAX };
enum { TIMELINE_BUFSZ = 500 };

struct message {
	bool edited;
	bool formatted;
	bool redacted;
	bool reply;
	uint64_t index; /* Index from database. */
	uint64_t
	  index_reply; /* Index (from database) of the message being replied to. */
	char *body;	   /* HTML if formatted is true. */
	char *sender;
};

struct timeline {
	struct message *buf; /* Size is capped to avoid realloc and make it easier
						  * to access from multiple threads. */
	_Atomic size_t len; /* Index of the last inserted message, must be an atomic
						 * since we copy it in the syncer thread, fill in events
						 * and set it back. */
};

struct room {
	uint64_t current_message_index;
	struct room_info *info;
	/* We use binary search to search for events. We require
	 * insertion at both the front and back so an ordered hash-map
	 * is not feasible. We use an array of fixed-size buffers instead.
	 * Insertion of new events starts from TIMELINE_FORWARD and the more
	 * buffers are added as required. Paginated events are stored similarly
	 * in TIMELINE_BACKWARD. */
	struct timeline *timelines[TIMELINE_MAX];
	/* Mutex for realloc-ing the timelines array or marking edited/redacted
	 * flags on events. Must be locked for the full duration duration of an
	 * iteration by the reader thread. The writer thread locks it on a per-event
	 * basis to avoid blocking for too long. This is fine as edits or redactions
	 * are way less common than regular events, which we don't need to lock for
	 * and can just fill in arrays after the len parameter of the timeline. */
	pthread_mutex_t nontrivial_modification_mutex;
};

struct room_index {
	size_t index_timeline;
	size_t index_buf;
	size_t index_message;
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
