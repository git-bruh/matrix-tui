#undef calloc
#undef malloc
#undef realloc
#undef strdup
#undef strndup

#include "state.h"

#include <string.h>

#define SIZE(arr) (sizeof(arr) / sizeof(*(arr)))

_Noreturn void
fatal_die(void) {
#define ESC "\033"
	/* Stolen from busybox's 'console-tools/reset.c'.
	 * We can't call tb_shutdown() as it is not thread safe. */
	char reset[] = ESC "c" ESC "(B" ESC "[m" ESC "[J" ESC "[?25h";
#undef ESC

	char oom[] = "Out Of Memory\n";

	/* Reset the terminal first. */
	write(STDERR_FILENO, reset, SIZE(reset) - 1);
	write(STDERR_FILENO, oom, SIZE(oom) - 1);

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
