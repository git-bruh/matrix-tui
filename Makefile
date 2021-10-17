.POSIX:

.PHONY: third_party format tidy clean

include common.mk

BIN = client

XCFLAGS = \
	$(CFLAGS_COMMON) -Wcast-qual -Wconversion -Wpointer-arith \
	-Wunused-macros -Wredundant-decls \
	-DCLIENT_NAME=\"matrix-client\"

LDLIBS = `curl-config --libs` -lpthread

INCLUDES = \
	-I libmatrix_src \
	-isystem third_party/cJSON \
	-isystem third_party/log.c/src \
	-isystem third_party/stb \
	-isystem third_party/termbox/src

OBJ = \
	src/buffer.o \
	src/input.o \
	src/main.o \
	libmatrix_src/api.o \
	libmatrix_src/matrix.o \
	libmatrix_src/sync.o \
	libmatrix_src/utils.o

all: release

.c.o:
	$(CC) $(XCFLAGS) $(INCLUDES) $(CPPFLAGS) -c $< -o $@

# Makefile pattern matching isn't portable so we must use this hack :(
third_party:
	$(MAKE) -f third_party.mk

$(BIN): $(OBJ) third_party
	$(CC) $(XCFLAGS) -o $@ $(OBJ) $(THIRD_PARTY_OBJ) $(LDLIBS) $(LDFLAGS)

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
	clang-format -i ./*src/*.[hc]

tidy:
	clang-tidy ./*src/*.[hc] -- $(XCFLAGS) $(INCLUDES) \
		-DNDEBUG # Prevent assertions from increasing cognitive complexity.

iwyu:
	for src in ./*src/*.c; do \
		include-what-you-use $$src $(XCFLAGS) $(INCLUDES) ||:; \
	done

clean:
	rm -f $(BIN) $(OBJ)
	$(MAKE) -f third_party.mk clean
