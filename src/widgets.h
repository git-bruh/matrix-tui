#ifndef WIDGETS_H
#define WIDGETS_H
/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "termbox.h"

#include <stdbool.h>

enum { WIDGET_CH_MAX = 2 }; /* Max width. */

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

uint32_t
widget_uc_sanitize(uint32_t uc, int *width);
bool
widget_points_in_bounds(const struct widget_points *points, int x, int y);
bool
widget_should_forcebreak(int width);
bool
widget_should_scroll(int x, int width, int max_width);
/* Returns the number of times y was advanced. */
int
widget_adjust_xy(int width, const struct widget_points *points, int *x, int *y);
int
widget_print_str(
  int x, int y, int max_x, uintattr_t fg, uintattr_t bg, const char *str);

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
	int start_y;
	size_t cur_buf; /* Current position inside buf. */
	uint32_t *buf;	/* We use a basic array instead of something like a linked
					 * list of small arrays  or a gap buffer as pretty much all
					 * messages are small enough that array
					 * insertion / deletion performance isn't an issue. */
	struct widget_callback cb;
};

int
input_init(struct input *input, struct widget_callback cb);
void
input_finish(struct input *input);
void
input_redraw(struct input *input);
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
	int skipped; /* A hack used to skip lines in recursive rendering. */
	int start_y;
	struct treeview_node *root;
	struct treeview_node *selected;
	struct widget_callback cb;
};

int
treeview_init(struct treeview *treeview, struct widget_callback cb);
void
treeview_finish(struct treeview *treeview);
void
treeview_redraw(struct treeview *treeview);
enum widget_error
treeview_event(struct treeview *treeview, enum treeview_event event);
#endif /* !WIDGETS_H */
