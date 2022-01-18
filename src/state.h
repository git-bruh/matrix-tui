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

#define read safe_read
#define write safe_write
#endif /* !STATE_H */
