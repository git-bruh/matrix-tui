.POSIX:

.PHONY: clean

include common.mk

XCFLAGS = $(CFLAGS_COMMON) -DLOG_USE_COLOR -DTB_OPT_TRUECOLOR

all: $(THIRD_PARTY_OBJ)

.c.o:
	$(CC) $(XCFLAGS) $(CPPFLAGS) -c $< -o $@

clean:
	rm -f $(THIRD_PARTY_OBJ)
