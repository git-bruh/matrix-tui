BIN = client

XCFLAGS = $(CFLAGS) -O3 -std=c99 -g -fstack-protector-strong \
		  --param=ssp-buffer-size=4 -D_FORTIFY_SOURCE=2 -DNAME=\"$(BIN)\" \
		  -Wall -Wextra -Werror -Wpedantic -Wcast-qual -Wconversion \
		  -Wc++-compat -Wshadow -Wnull-dereference -Wpointer-arith -Wunused-macros \
		  -Wredundant-decls -Wformat=2 \
		  -D_XOPEN_SOURCE

LDLIBS = # -llmdb `pkg-config --libs libcurl`

OBJ = \
	src/buffer.o \
	src/input.o \
	third_party/termbox/src/termbox.o \
	third_party/termbox/src/utf8.o

all: $(BIN)

%.o: %.c
	$(CC) $(XCFLAGS) -isystem third_party -c -o $@ $<

third_party/%.o: third_party/%.c
	$(CC) $(XCFLAGS) -w -c -o $@ $<

$(BIN): $(OBJ)
	$(CC) $(XCFLAGS) -o $@ $(OBJ) $(LDLIBS) $(LDFLAGS)

format:
	clang-format -i src/*.c

sanitize:
	$(MAKE) CFLAGS="$(CFLAGS) -fsanitize=address,undefined"

clean:
	rm -f $(BIN) $(OBJ)
