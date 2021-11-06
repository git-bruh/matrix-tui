#ifndef WIDGETS_H
#define WIDGETS_H
/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "termbox.h"
#include <stdbool.h>

enum widget_type { WIDGET_INPUT = 0, WIDGET_TREEVIEW };

enum widget_error { WIDGET_NOOP = 0, WIDGET_REDRAW };

/* The rectangle in which the widget will be drawn. */
struct widget_points {
	int x1; /* x of top-left corner. */
	int x2; /* x of bottom-right corner. */
	int y1; /* y of top-left corner. */
	int y2; /* y of bottom-right corner. */
};

struct widget_callback {
	void *userp;
	void (*cb)(enum widget_type, struct widget_points *, void *);
};

/* Input. */
enum input_event {
	INPUT_DELETE = 0,
	INPUT_DELETE_WORD,
	INPUT_RIGHT,
	INPUT_RIGHT_WORD,
	INPUT_LEFT,
	INPUT_LEFT_WORD,
	INPUT_ADD /* Must pass an uint32_t argument. */
};

struct input {
	int max_height;
	int line_off; /* The number of lines to skip when rendering the buffer. */
	int last_cur_line; /* The "line" where the cursor was placed in the last
						  draw, used to advance / decrement cur_y and line_off.
						*/
	int cur_y; /* y co-ordinate relative to the input field (not window). */
	size_t cur_buf; /* Current position inside buf. */
	uint32_t *buf;	/* We use a basic array instead of something like a linked
					 * list of small arrays  or a gap buffer as pretty much all
					 * messages are small enough that array
					 * insertion / deletion performance isn't an issue. */
	struct widget_callback cb;
};

int
input_init(struct input *input, int input_height, struct widget_callback cb);
void
input_finish(struct input *input);
void
input_redraw(struct input *input);
void
input_set_initial_cursor(struct input *input);
enum widget_error
input_handle_event(struct input *input, enum input_event event, ...);

/* Treeview. */
enum treeview_event {
	TREEVIEW_EXPAND = 0,
	TREEVIEW_UP,
	TREEVIEW_DOWN,
	TREEVIEW_INSERT,		/* Add a child node to the node. */
	TREEVIEW_INSERT_PARENT, /* Add a node to the node's parent node. */
	TREEVIEW_DELETE,		/* Delete a node along with it's children. */
};

struct treeview_node {
	bool is_expanded; /* Whether it's children are visible. */
	size_t index;	  /* Index in the **trees array. */
	char *string;
	void *data; /* Any user data. */
	struct treeview_node *parent;
	struct treeview_node **nodes;
};

struct treeview {
	int skipped;
	int start_y;
	struct tree *root;
	struct tree *selected;
	struct widget_callback cb;
};

int
treeview_init(struct treeview *treeview, struct widget_callback cb);
void
treeview_finish(struct treeview *treeview);
enum widget_error
treeview_event(struct treeview *treeview, enum treeview_event event);
#endif /* !WIDGETS_H */
