#ifndef FATAL_ASSERT_H
#define FATAL_ASSERT_H
#undef assert
#ifdef NDEBUG
#define assert(x) (void) 0
#else
_Noreturn static inline void
fatal_assert_fail(
  const char *expr, const char *file, int line, const char *func) {
	fprintf(
	  stderr, "Assertion failed: %s (%s: %s: %d)\n", expr, file, func, line);
	abort();
}

#define assert(x)                                                              \
	((void) ((x) || (fatal_assert_fail(#x, __FILE__, __LINE__, __func__), 0)))
#endif /* NDEBUG */
#endif /* !FATAL_ASSERT_H */
