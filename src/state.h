#ifndef STATE_H
#define STATE_H
#include "cache.h"
#include "queue.h"

#include <errno.h>
#include <unistd.h>
enum { THREAD_SYNC = 0, THREAD_QUEUE, THREAD_MAX };
enum { PIPE_READ = 0, PIPE_WRITE, PIPE_MAX };
enum { FD_TTY = 0, FD_RESIZE, FD_PIPE, FD_MAX };

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
	struct {
		char *key;
		struct room *value;
	} * rooms;
	pthread_mutex_t rooms_mutex;
};

/* Wrapper for read() / write() immune to EINTR. */
__attribute__((unused)) static ssize_t
safe_read_or_write(int fildes, void *buf, size_t nbyte, int what) {
	ssize_t ret = 0;

	while (nbyte > 0) {
		do {
			ret = (what == 0) ? (read(fildes, buf, nbyte))
							  : (write(fildes, buf, nbyte));
		} while ((ret < 0) && (errno == EINTR || errno == EAGAIN));

		if (ret < 0) {
			return ret;
		}

		nbyte -= (size_t) ret;
		/* Increment buffer */
		buf = &((unsigned char *) buf)[ret];
	}

	return 0;
}

#define read(fildes, buf, byte) safe_read_or_write(fildes, buf, byte, 0)
#define write(fildes, buf, nbyte) safe_read_or_write(fildes, buf, nbyte, 1)
#endif /* !STATE_H */
