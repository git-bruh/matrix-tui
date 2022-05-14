#include <errno.h>
#include <stdint.h>
#include <unistd.h>

/* Wrapper for read() / write() immune to EINTR.
 * Must be a macro so we can generate 2 functions with const and
 * non-const args. This is for use in program code, not library code. */
#define GEN_WRAPPER(name, func, increment_type, ...)                           \
	ssize_t name(__VA_ARGS__) {                                                \
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
