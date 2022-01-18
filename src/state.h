#ifndef STATE_H
#define STATE_H
#include "cache.h"
#include "queue.h"

#include <errno.h>
#include <unistd.h>

enum { THREAD_SYNC = 0, THREAD_QUEUE, THREAD_MAX };
enum { PIPE_READ = 0, PIPE_WRITE, PIPE_MAX };
enum { FD_TTY = 0, FD_RESIZE, FD_PIPE, FD_MAX };

struct hm_room {
	char *key;
	struct room *value;
};

struct state {
	_Atomic bool done;
	pthread_t threads[THREAD_MAX];
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	struct matrix *matrix;
	struct cache cache;
	struct queue queue;
	/* Pass data between the syncer thread and the UI thread. This exists as the
	 * UI thread can't block forever, listening to a queue as it has to handle
	 * input and resizes. Instead, we poll on this pipe along with polling for
	 * events from the terminal. */
	int thread_comm_pipe[PIPE_MAX];
	struct hm_room *rooms;
	pthread_mutex_t rooms_mutex;
};

/* Wrapper for read() / write() immune to EINTR.
 * Must be a macro so we can generate 2 functions with const and
 * non-const args. This is also not declared in fatal.h so that
 * we only override our code, and not external library code. */
#define GEN_WRAPPER(name, func, increment_type, ...)                           \
	__attribute__((unused)) static ssize_t name(__VA_ARGS__) {                 \
		ssize_t ret = 0;                                                       \
		while (nbyte > 0) {                                                    \
			do {                                                               \
				ret = func(fildes, buf, nbyte);                                \
			} while ((ret < 0) && (errno == EINTR || errno == EAGAIN));        \
			if (ret < 0) {                                                     \
				return ret;                                                    \
			}                                                                  \
			nbyte -= (size_t) ret; /* Increment buffer */                      \
			buf = &((increment_type *) buf)[ret];                              \
		}                                                                      \
		return 0;                                                              \
	}

GEN_WRAPPER(safe_read, read, uint8_t, int fildes, void *buf, size_t nbyte)
GEN_WRAPPER(
  safe_write, write, const uint8_t, int fildes, const void *buf, size_t nbyte)

#undef GEN_WRAPPER

#define read safe_read
#define write safe_write
#endif /* !STATE_H */
