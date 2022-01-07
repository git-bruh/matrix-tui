#undef calloc
#undef malloc
#undef realloc
#undef strdup
#undef strndup

#include "state.h"

#include <stdio.h>
#include <string.h>

#define SIZE(arr) (sizeof(arr) / sizeof(*(arr)))

static void
reset_term(void) {
#define ESC "\033"
	/* Stolen from busybox's 'console-tools/reset.c'.
	 * We can't call tb_shutdown() as it is not thread safe. */
	char reset[] = ESC "c" ESC "(B" ESC "[m" ESC "[J" ESC "[?25h";
#undef ESC

	write(STDOUT_FILENO, reset, SIZE(reset) - 1);
}

_Noreturn void
fatal_die(void) {
	reset_term();
	char oom[] = "Out Of Memory\n";
	write(STDOUT_FILENO, oom, SIZE(oom) - 1);
	abort();
}

_Noreturn void
fatal_assert_fail(
  const char *expr, const char *file, int line, const char *func) {
	reset_term();
	printf("Assertion failed: %s (%s: %s: %d)\n", expr, file, func, line);
	abort();
}

static void *
return_or_fatal(void *ptr) {
	if (ptr) {
		return ptr;
	}

	fatal_die();
}

void *
fatal_calloc(size_t nelem, size_t elsize) {
	return return_or_fatal(calloc(nelem, elsize));
}

void *
fatal_realloc(void *ptr, size_t size) {
	return return_or_fatal(realloc(ptr, size));
}

char *
fatal_strdup(const char *s) {
	return (char *) return_or_fatal(strdup(s));
}

char *
fatal_strndup(const char *s, size_t size) {
	return (char *) return_or_fatal(strndup(s, size));
}
