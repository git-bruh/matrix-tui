#undef abort
#undef calloc
#undef malloc
#undef realloc
#undef reallocarray
#undef strdup
#undef strndup

#include "util/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Escape stolen from busybox's 'console-tools/reset.c'.
 * We can't call tb_shutdown() as it is not thread safe. */
#define FATAL_PRINT_DIE(s, ...)                                                \
	do {                                                                       \
		printf("%s", "\033c\033(B\033[m\033[J\033[?25h");                      \
		printf(s, __VA_ARGS__);                                                \
		fflush(stdout);                                                        \
		abort();                                                               \
	} while (0)

#define FATAL_CHECK(ptr)                                                       \
	do {                                                                       \
		__typeof__((ptr)) _tmp = (ptr);                                        \
		if (!_tmp) {                                                           \
			FATAL_PRINT_DIE("%s\n", "Out Of Memory");                          \
		}                                                                      \
		return _tmp;                                                           \
	} while (0)

void
fatal_abort(void) {
	FATAL_PRINT_DIE(
	  "The program encountered a fatal bug. Please file an issue at " BUG_URL
	  " with the contents of '%s'\n",
	  log_path());
}
void *
fatal_calloc(size_t nelem, size_t elsize) {
	FATAL_CHECK(calloc(nelem, elsize));
}
void *
fatal_malloc(size_t size) {
	FATAL_CHECK(malloc(size));
}
void *
fatal_realloc(void *ptr, size_t size) {
	FATAL_CHECK(realloc(ptr, size));
}
void *
fatal_reallocarray(void *ptr, size_t nmemb, size_t size) {
	FATAL_CHECK(reallocarray(ptr, nmemb, size));
}
char *
fatal_strdup(const char *s) {
	FATAL_CHECK(strdup(s));
}
char *
fatal_strndup(const char *s, size_t size) {
	FATAL_CHECK(strndup(s, size));
}

#undef FATAL_CHECK
#undef FATAL_PRINT_DIE
