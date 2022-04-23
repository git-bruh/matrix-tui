#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

static const char *const log_file = "matrix-tui.log";

/* NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables) */
static char log_path_static[PATH_MAX] = {0};
/* NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables) */
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

void
log_path_set(void) {
	/* Only called at the beginning of main() */
	/* NOLINTNEXTLINE(concurrency-mt-unsafe) */
	const char *tmpdir = getenv("TMPDIR");

	if (!tmpdir) {
		tmpdir = "/tmp";
	}

	snprintf(log_path_static,
	  sizeof(log_path_static) / sizeof(log_path_static[0]), "%s/%s", tmpdir,
	  log_file);
}

const char *
log_path(void) {
	return log_path_static;
}

void
log_mutex_lock(void) {
	pthread_mutex_lock(&log_mutex);
}

void
log_mutex_unlock(void) {
	pthread_mutex_unlock(&log_mutex);
}

void
log_mutex_destroy(void) {
	pthread_mutex_destroy(&log_mutex);
}
