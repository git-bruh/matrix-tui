#ifndef FATAL_H
#define FATAL_H
#include <stdlib.h>
enum fatal_error { FATAL_OOM = 0, FATAL_MAX };

extern const char *const fatal_errors[FATAL_MAX];

_Noreturn void
fatal_die(const char *error);

void *
fatal_realloc(void *ptr, size_t size);
char *
fatal_strdup(const char *s);

#ifndef FATAL_NO_MACROS
#define fatal_malloc(size) fatal_realloc(NULL, size)
#define malloc(size) fatal_malloc(size)
#define realloc(ptr, size) fatal_realloc(ptr, size)
#define strdup(s) fatal_strdup(s)
#else
#undef FATAL_NO_MACROS
#endif /* !FATAL_NO_MACROS */
#endif /* !FATAL_H */
