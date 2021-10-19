.POSIX:

CFLAGS_COMMON = \
	$(CFLAGS) -O3 -std=c11 \
	-flto -fstack-protector-strong --param=ssp-buffer-size=4 \
	-D_GNU_SOURCE -D_XOPEN_SOURCE -D_FORTIFY_SOURCE=2 \
	-Wall -Wextra -Wpedantic -Wwrite-strings \
	-Wshadow -Wnull-dereference -Wformat=2 \
	-Werror=implicit-function-declaration \
	-Werror=incompatible-pointer-types \
	-Werror=discarded-qualifiers \
	-Werror=ignored-qualifiers

THIRD_PARTY_OBJ = \
	third_party/cJSON/cJSON.o \
	third_party/log.c/src/log.o \
	third_party/termbox/src/termbox.o \
	third_party/termbox/src/utf8.o
