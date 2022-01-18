#ifndef FATAL_H
#define FATAL_H
/* This file must be included in every source file with the --include option. */
#include <stdlib.h>

_Noreturn void
fatal_die(const char *msg);

void *
fatal_calloc(size_t nelem, size_t elsize);
void *
fatal_realloc(void *ptr, size_t size);
char *
fatal_strdup(const char *s);
char *
fatal_strndup(const char *s, size_t size);

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
