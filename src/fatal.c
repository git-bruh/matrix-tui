#define FATAL_NO_MACROS
#include "fatal.h"

#include <string.h>
#include <unistd.h>

#define SIZE(arr) (sizeof(arr) / sizeof(*(arr)))

const char *const fatal_errors[FATAL_MAX] = {
  [FATAL_OOM] = "Out Of Memory",
};

ssize_t
safe_read_or_write(int fildes, void *buf, size_t nbyte, int what);

#define write(fildes, buf, nbyte) safe_read_or_write(fildes, buf, nbyte, 1)

static char *
noconst(const char *str) {
	return (char *) str;
}

_Noreturn void
fatal_die(const char *error) {
#define ESC "\033"
	/* Stolen from busybox's 'console-tools/reset.c'.
	 * We can't call tb_shutdown() as it is not thread safe. */
	const char reset[] = ESC "c" ESC "(B" ESC "[m" ESC "[J" ESC "[?25h";
#undef ESC

	const char msg[] = "The program encountered a fatal error: '";
	const char fix_terminal[] = "'. Run `reset` to fix your terminal.\n";

	size_t len = strlen(error);

	/* Reset the terminal first. */
	write(STDERR_FILENO, noconst(reset), SIZE(reset) - 1);
	write(STDERR_FILENO, noconst(msg), SIZE(msg) - 1);
	write(STDERR_FILENO, noconst(error), len);
	write(STDERR_FILENO, noconst(fix_terminal), SIZE(fix_terminal) - 1);

	exit(EXIT_FAILURE);
}

void *
fatal_realloc(void *ptr, size_t size) {
	void *tmp = realloc(ptr, size);

	if (tmp) {
		return tmp;
	}

	fatal_die(fatal_errors[FATAL_OOM]);
}

char *
fatal_strdup(const char *s) {
	char *tmp = strdup(s);

	if (tmp) {
		return tmp;
	}

	fatal_die(fatal_errors[FATAL_OOM]);
}
