.POSIX:

.PHONY: third_party format tidy clean

include common.mk

BIN = client

XCFLAGS = \
	$(CFLAGS_COMMON) -Wcast-qual -Wconversion -Wpointer-arith \
	-Wunused-macros -Wredundant-decls

LDLIBS = `curl-config --libs` -lev

INCLUDES = \
	-I libmatrix_src \
	-isystem third_party/cJSON \
	-isystem third_party/stb \
	-isystem third_party/termbox/src

OBJ = \
	src/buffer.o \
	src/input.o \
	src/main.o \
	libmatrix_src/api.o \
	libmatrix_src/dispatch.o \
	libmatrix_src/matrix.o

all: release

.c.o:
	$(CC) $(XCFLAGS) $(INCLUDES) -c $< -o $@

# Makefile pattern matching isn't portable so we must use this hack :(
third_party:
	$(MAKE) -f third_party.mk

$(BIN): $(OBJ) third_party
	$(CC) $(XCFLAGS) -o $@ $(OBJ) $(THIRD_PARTY_OBJ) $(LDFLAGS) $(LDLIBS)

release:
	$(MAKE) $(BIN) \
		CFLAGS="$(CFLAGS) -DNDEBUG"

release-static:
	$(MAKE) $(BIN) \
		CFLAGS="$(CFLAGS) -DNDEBUG" \
		LDFLAGS="$(LDFLAGS) -static" \
		LDLIBS="$(LDLIBS) `curl-config --static-libs`"

sanitize:
	$(MAKE) $(BIN) \
		CFLAGS="$(CFLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer -g3"

format:
	clang-format -i ./*src/*.c ./*src/*.h

tidy:
	clang-tidy ./*src/*.c ./*src/*.h -- $(XCFLAGS) $(INCLUDES)

clean:
	rm -f $(BIN) $(OBJ)
	$(MAKE) -f third_party.mk clean
