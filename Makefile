.POSIX:

.PHONY: format tidy clean

BIN = client

XCFLAGS = \
	$(CFLAGS) $(CPPFLAGS) -O3 -std=c11 \
	-D_GNU_SOURCE -D_FORTIFY_SOURCE=2 \
	-flto -fstack-protector-strong --param=ssp-buffer-size=4 \
	-Wall -Wextra -Wpedantic -Wwrite-strings \
	-Wshadow -Wnull-dereference -Wformat=2 \
	-Wcast-qual -Wconversion -Wpointer-arith \
	-Wunused-macros -Wredundant-decls \
	-Werror=implicit-function-declaration \
	-Werror=incompatible-pointer-types \
	-DLOG_USE_COLOR \
	-DCLIENT_NAME=\"matrix-client\"

LDLIBS = `curl-config --libs` -lcjson -lpthread

INCLUDES = \
	-I libmatrix_src \
	-isystem third_party/log.c/src \
	-isystem third_party/stb \
	-isystem third_party/termbox2

OBJ = \
	src/header_libs.o \
	src/input.o \
	src/tree.o \
	src/ui_common.o \
	src/main.o \
	libmatrix_src/api.o \
	libmatrix_src/matrix.o \
	libmatrix_src/sync.o \
	libmatrix_src/utils.o \
	third_party/log.c/src/log.o

all: release

.c.o:
	$(CC) $(XCFLAGS) $(INCLUDES) $(CPPFLAGS) -c $< -o $@

$(BIN): $(OBJ)
	$(CC) $(XCFLAGS) -o $@ $(OBJ) $(LDLIBS) $(LDFLAGS)

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
	clang-format -Wno-error=unknown -i ./*src/*.[hc]

tidy:
	clang-tidy ./*src/*.[hc] -- $(XCFLAGS) $(INCLUDES)

clean:
	rm -f $(BIN) $(OBJ)
