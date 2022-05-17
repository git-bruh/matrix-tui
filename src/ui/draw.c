/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "app/hm_room.h"
#include "app/room_ds.h"
#include "ui/message_buffer.h"
#include "ui/ui.h"
#include "widgets.h"

#include <assert.h>
#include <math.h>

enum {
	INPUT_HEIGHT = 5,
	INPUT_BORDER_PX = 2, /* Border above and below the field. */
	FORM_WIDTH = 68,
	FORM_ART_GAP = 2,
	TAB_ROOM_TREE_PERCENT = 20,
	BORDER_HIGHLIGHT_FG = COLOR_BLUE,
};

/* Wrapper for drawing and highlighting borders. */
static void
border_highlight(struct widget_points *points, bool highlight) {
	border_redraw(
	  points, highlight ? BORDER_HIGHLIGHT_FG : TB_DEFAULT, TB_DEFAULT);
}

static int
part_percent(int total, int percent) {
	/* 10% of 80 == (10 * 80) / 100 */
	const double percent_to_value = 100.0;
	return (int) round((total * percent) / percent_to_value);
}

static void
art_redraw(struct widget_points *points) {
	const char *art[]
	  = {"███╗███╗   ███╗ █████╗ ████████╗██████╗ ██╗██╗  ██╗███╗",
		"██╔╝████╗ ████║██╔══██╗╚══██╔══╝██╔══██╗██║╚██╗██╔╝╚██║",
		"██║ ██╔████╔██║███████║   ██║   ██████╔╝██║ ╚███╔╝  ██║",
		"██║ ██║╚██╔╝██║██╔══██║   ██║   ██╔══██╗██║ ██╔██╗  ██║",
		"███╗██║ ╚═╝ ██║██║  ██║   ██║   ██║  ██║██║██╔╝ ██╗███║",
		"╚══╝╚═╝     ╚═╝╚═╝  ╚═╝   ╚═╝   ╚═╝  ╚═╝╚═╝╚═╝  ╚═╝╚══╝"};

	int y = points->y2;

	int padding
	  = widget_pad_center(widget_str_width(*art), points->x2 - points->x1);

	for (size_t i = (sizeof(art) / sizeof(*art)); i > 0 && y >= points->y1;
		 i--, y--) {
		widget_print_str(
		  padding, y, points->x2, TB_DEFAULT, TB_DEFAULT, art[i - 1]);
	}
}

void
tab_login_redraw(struct tab_login *login) {
	assert(login);

	int height = tb_height();
	int width = tb_width();

	struct widget_points points = {0};

	int padding_x = widget_pad_center(FORM_WIDTH, width);
	int padding_y = widget_pad_center(FORM_HEIGHT, height);

	widget_points_set(&points, 0, width, 0, padding_y - FORM_ART_GAP);
	art_redraw(&points);

	widget_points_set(
	  &points, padding_x, width - padding_x, padding_y, height - padding_y);
	form_redraw(&login->form, &points);

	if (login->error) {
		widget_print_str(
		  widget_pad_center(widget_str_width(login->error), width),
		  (height - padding_y) + 1, width, COLOR_RED, TB_DEFAULT, login->error);
	}

	/* Don't show input field cursor if button is active. */
	if (login->form.button_is_selected) {
		tb_hide_cursor();
	}
}

static void
adjust_inside_border(struct widget_points *points) {
	assert(points);

	widget_points_set(
	  points, points->x1 + 1, points->x2 - 1, points->y1 + 1, points->y2 - 1);
}

static void
get_tree_points(struct widget_points *points) {
	assert(points);

	widget_points_set(points, 0,
	  part_percent(tb_width(), TAB_ROOM_TREE_PERCENT), 0, tb_height());
}

static void
get_input_points(
  struct widget_points *points, struct input *input, int *input_rows) {
	assert(points);
	assert(input);
	assert(input_rows);

	struct widget_points tree_points = {0};
	get_tree_points(&tree_points);

	widget_points_set(points, tree_points.x2, tb_width(),
	  tb_height() - INPUT_HEIGHT - INPUT_BORDER_PX, tb_height());

	adjust_inside_border(points);
	input_redraw(input, points, input_rows, true);

	assert(*input_rows >= 0);

	if (*input_rows == 0) {
		*input_rows = 1;
	}

	/* -1 -1 for 2 border on 2 sides. */
	widget_points_set(points, tree_points.x2, tb_width(),
	  tb_height() - *input_rows - INPUT_BORDER_PX, tb_height());
}

static void
get_buffer_points(struct widget_points *points, int input_rows) {
	assert(points);
	assert(input_rows > 0);

	struct widget_points tree_points = {0};
	get_tree_points(&tree_points);

	widget_points_set(points, tree_points.x2, tb_width(), 0,
	  tb_height() - input_rows - INPUT_BORDER_PX);
}

/* Excluding border. */
void
tab_room_get_buffer_points(struct widget_points *points) {
	get_buffer_points(points, 1);
	adjust_inside_border(points);
}

void
tab_room_get_points(
  struct tab_room *tab_room, struct widget_points points[TAB_ROOM_MAX]) {
	assert(tab_room);
	assert(points);

	int input_rows = -1;

	get_tree_points(&points[TAB_ROOM_TREE]);
	get_input_points(&points[TAB_ROOM_INPUT], &tab_room->input, &input_rows);
	get_buffer_points(&points[TAB_ROOM_MESSAGE_BUFFER], input_rows);
}

void
tab_room_redraw(struct tab_room *tab_room) {
	assert(tab_room);

	struct widget_points points = {0};
	const enum tab_room_widget active = tab_room->widget;

	get_tree_points(&points);
	border_highlight(&points, active == TAB_ROOM_TREE);
	adjust_inside_border(&points);
	treeview_redraw(&tab_room->treeview, &points);

	int input_rows = -1;

	get_input_points(&points, &tab_room->input, &input_rows);
	border_highlight(&points, active == TAB_ROOM_INPUT);
	adjust_inside_border(&points);

	/* Don't pass input_rows as we already set it. */
	input_redraw(&tab_room->input, &points, &(int) {0}, false);

	/* Draw border even if no active room. */
	get_buffer_points(&points, input_rows);
	border_highlight(&points, active == TAB_ROOM_MESSAGE_BUFFER);

	if (tab_room->selected_room) {
		struct room *room = tab_room->selected_room->value;

		adjust_inside_border(&points);
		pthread_mutex_lock(&room->realloc_or_modify_mutex);
		message_buffer_redraw(&room->buffer, &points);
		pthread_mutex_unlock(&room->realloc_or_modify_mutex);
	}
}
