.POSIX:

BIN = client
LIB = libmatrix.h

.PHONY: $(LIB) format tidy clean

include config.mk

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

XLDLIBS = $(LDLIBS) `curl-config --libs` -lcjson -lpthread

INCLUDES = \
	-I libmatrix_src \
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
	libmatrix_src/utils.o

all: $(BIN)

# Track header file changes.
DEP = $(OBJ:.o=.d)
-include $(DEP)

.c.o:
	$(CC) $(XCFLAGS) $(INCLUDES) $(CPPFLAGS) -MMD -c $< -o $@

$(BIN): $(OBJ)
	$(CC) $(XCFLAGS) -o $@ $(OBJ) $(XLDLIBS) $(LDFLAGS)

$(LIB):
	@{ \
		:>$(LIB); \
		printf "%s\n" \
			"#ifndef LIBMATRIX_H" \
			"#define LIBMATRIX_H" >> $(LIB); \
		cat ./libmatrix_src/matrix.h >> $(LIB); \
		printf "%s\n" \
			"#endif" \
			"#ifdef LIBMATRIX_IMPL" \
			>> $(LIB); \
		cat ./libmatrix_src/matrix-priv.h >> $(LIB); \
		cat ./libmatrix_src/*.c >> $(LIB); \
		printf "%s\n" \
			"#endif" >> $(LIB); \
		sed -i '/^#include \"/d' $(LIB); \
	}

format:
	clang-format -Wno-error=unknown -i ./*src/*.[hc]

tidy:
	clang-tidy ./*src/*.[hc] -- $(XCFLAGS) $(INCLUDES)

clean:
	rm -f $(DEP) $(OBJ) $(BIN) $(LIB)
