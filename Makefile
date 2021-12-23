.POSIX:

BIN = client

.PHONY: format tidy clean

include config.mk

XCFLAGS = \
	$(CFLAGS) $(CPPFLAGS) -O3 -std=c11 \
	-D_GNU_SOURCE -D_FORTIFY_SOURCE=2 \
	-flto -fstack-protector-strong --param=ssp-buffer-size=4 \
	-Wall -Wextra -Wpedantic -Wshadow -Wnull-dereference \
	-Wformat=2 -Wcast-qual -Wconversion -Wpointer-arith \
	-Wunused-macros -Wredundant-decls -Wwrite-strings \
	-Werror=int-conversion \
	-Werror=implicit-function-declaration \
	-Werror=incompatible-pointer-types \
	-DTB_OPT_TRUECOLOR \
	-DCLIENT_NAME=\"matrix-tui\"

XLDLIBS = $(LDLIBS) `curl-config --libs` -lcjson -llmdb -lpthread

INCLUDES = \
	-I third_party/libmatrix \
	-I third_party/termbox-widgets \
	-isystem third_party/stb \
	-isystem third_party/termbox2

OBJ = \
	src/header_libs.o \
	src/queue.o \
	src/cache.o \
	src/draw.o \
	src/room_ds.o \
	src/login_form.o \
	src/render_message.o \
	src/message_buffer.o \
	src/main.o \
	third_party/libmatrix/api.o \
	third_party/libmatrix/linked_list.o \
	third_party/libmatrix/matrix.o \
	third_party/libmatrix/sync.o \
	third_party/termbox-widgets/input.o \
	third_party/termbox-widgets/tree.o \
	third_party/termbox-widgets/ui_common.o

all: $(BIN)

# Track header file changes.
DEP = $(OBJ:.o=.d)
-include $(DEP)

.c.o:
	$(CC) $(XCFLAGS) $(INCLUDES) $(CPPFLAGS) -MMD -c $< -o $@

$(BIN): $(OBJ)
	$(CC) $(XCFLAGS) -o $@ $(OBJ) $(XLDLIBS) $(LDFLAGS)

format:
	clang-format -Wno-error=unknown -i ./*src/*.[hc]

tidy:
	clang-tidy ./*src/*.[hc] -- $(XCFLAGS) $(INCLUDES)

clean:
	rm -f $(DEP) $(OBJ) $(BIN)
