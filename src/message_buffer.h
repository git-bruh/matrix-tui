#ifndef MESSAGE_BUFFER_H
#define MESSAGE_BUFFER_H
/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "widgets.h"

struct buf_item;
struct message;

struct message_buffer {
	bool zeroed;
	size_t scroll;
	struct buf_item *buf;
	struct widget_points last_points;
};

enum message_buffer_event {
	MESSAGE_BUFFER_UP = 0,
	MESSAGE_BUFFER_DOWN,
	MESSAGE_BUFFER_SELECT /* int argument of xy coordinates. */
};

void
message_buffer_finish(struct message_buffer *buf);
int
message_buffer_insert(struct message_buffer *buf, struct widget_points *points,
  struct message *message);
int
message_buffer_redact(struct message_buffer *buf, uint64_t index);
void
message_buffer_zero(struct message_buffer *buf);
void
message_buffer_ensure_sane_scroll(struct message_buffer *buf);
bool
message_buffer_should_recalculate(
  struct message_buffer *buf, struct widget_points *points);
enum widget_error
message_buffer_handle_event(
  struct message_buffer *buf, enum message_buffer_event event, ...);
void
message_buffer_redraw(struct message_buffer *buf, struct widget_points *points);
#endif /* !MESSAGE_BUFFER_H */
