.POSIX:

CFLAGS_COMMON = \
	$(CFLAGS) -O3 -std=c99 -g \
	-fstack-protector-strong --param=ssp-buffer-size=4 \
	-D_XOPEN_SOURCE -D_FORTIFY_SOURCE=2 \
	-Wall -Wextra -Wpedantic -Wshadow -Wnull-dereference -Wformat=2

THIRD_PARTY_OBJ = \
	third_party/termbox/src/termbox.o \
	third_party/termbox/src/utf8.o
