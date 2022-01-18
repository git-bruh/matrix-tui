#ifndef FATAL_H
#define FATAL_H
/* This file must be included in every source file with the --include option. */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define FATAL_DECL __attribute__((unused)) static inline

/* Wrapper for read() / write() immune to EINTR.
 * Must be a macro so we can generate 2 functions with const and
 * non-const args. This is for use in program code, not library code. */
#define GEN_WRAPPER(name, func, increment_type, ...)                           \
	FATAL_DECL ssize_t name(__VA_ARGS__) {                                     \
		ssize_t ret = 0;                                                       \
		while (nbyte > 0) {                                                    \
			do {                                                               \
				ret = func(fildes, buf, nbyte);                                \
			} while ((ret < 0) && (errno == EINTR || errno == EAGAIN));        \
			if (ret < 0) {                                                     \
				return ret;                                                    \
			}                                                                  \
			nbyte -= (size_t) ret;                                             \
			/* Increment buffer */                                             \
			buf = &((increment_type *) buf)[ret];                              \
		}                                                                      \
		return 0;                                                              \
	}

GEN_WRAPPER(safe_read, read, uint8_t, int fildes, void *buf, size_t nbyte)
GEN_WRAPPER(
  safe_write, write, const uint8_t, int fildes, const void *buf, size_t nbyte)

#undef GEN_WRAPPER

_Noreturn FATAL_DECL void
fatal_die(const char *msg) {
#define ESC "\033"
	/* Stolen from busybox's 'console-tools/reset.c'.
	 * We can't call tb_shutdown() as it is not thread safe. */
	char reset[] = ESC "c" ESC "(B" ESC "[m" ESC "[J" ESC "[?25h";
#undef ESC

	safe_write(STDOUT_FILENO, reset, strlen(reset));
	safe_write(STDOUT_FILENO, msg, strlen(msg));
	abort();
}

#define return_or_fatal(ptr)                                                   \
	do {                                                                       \
		__typeof__((ptr)) _tmp = (ptr);                                        \
		if (!_tmp) {                                                           \
			fatal_die("Out Of Memory\n");                                      \
		}                                                                      \
		return _tmp;                                                           \
	} while (0)

FATAL_DECL void *
fatal_calloc(size_t nelem, size_t elsize) {
	return_or_fatal(calloc(nelem, elsize));
}

FATAL_DECL void *
fatal_realloc(void *ptr, size_t size) {
	return_or_fatal(realloc(ptr, size));
}

FATAL_DECL char *
fatal_strdup(const char *s) {
	return_or_fatal(strdup(s));
}

FATAL_DECL char *
fatal_strndup(const char *s, size_t size) {
	return_or_fatal(strndup(s, size));
}

#undef return_or_fatal
#undef FATAL_DECL

#define fatal_abort()                                                          \
	fatal_die(                                                                 \
	  "The program encountered a fatal bug. Please file an issue at " BUG_URL  \
	  " with the contents of '" LOG_PATH "'\n")
#define fatal_malloc(size) fatal_realloc(NULL, size)
#define calloc(nelem, elsize) fatal_calloc(nelem, elsize)
#define malloc(size) fatal_malloc(size)
#define realloc(ptr, size) fatal_realloc(ptr, size)
#define strdup(s) fatal_strdup(s)
#define strndup(s, size) fatal_strndup(s, size)
#define abort() fatal_abort()
#endif /* !FATAL_H */
