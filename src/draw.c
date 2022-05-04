/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "message_buffer.h"
#include "room_ds.h"
#include "ui.h"
#include "widgets.h"

#include <assert.h>
#include <math.h>

enum {
	INPUT_HEIGHT = 5,
	FORM_WIDTH = 68,
	FORM_ART_GAP = 2,
	TAB_ROOM_TREE_PERCENT = 20,
};

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

void
tab_room_get_buffer_points(struct widget_points *points) {
	assert(points);
	int width = tb_width();

	widget_points_set(
	  points, part_percent(width, TAB_ROOM_TREE_PERCENT) + 1, width - 1, 0, 0);
}

void
tab_room_redraw(struct tab_room *tab_room) {
	assert(tab_room);

	if (!tab_room->selected_room) {
		return;
	}

	struct room *room = tab_room->selected_room->value;

	int height = tb_height();
	int width = tb_width();

	struct widget_points points = {0};

	int tree_width = part_percent(width, TAB_ROOM_TREE_PERCENT);

	widget_points_set(&points, tree_width + 1, width - 1,
	  height - INPUT_HEIGHT - 1, height - 1);

	int input_rows = 0;
	input_redraw(&tab_room->input, &points, &input_rows);

	const int input_border_start = height - 1 - input_rows - 1;
	const int message_border_end = input_border_start - 1;

	widget_points_set(&points, tree_width, width, input_border_start, height);
	border_redraw(&points, TB_DEFAULT, TB_DEFAULT);

	widget_print_str(
	  tree_width, message_border_end, width, TB_DEFAULT, TB_DEFAULT, "[");
	widget_print_str(
	  width - 1, message_border_end, width, TB_DEFAULT, TB_DEFAULT, "]");

	widget_points_set(&points, tree_width, width, 0, message_border_end);
	border_redraw(&points, TB_DEFAULT, TB_DEFAULT);

	widget_points_set(
	  &points, tree_width + 1, width - 1, 1, message_border_end - 1);

	pthread_mutex_lock(&room->realloc_or_modify_mutex);
	message_buffer_redraw(&room->buffer, &points);
	pthread_mutex_unlock(&room->realloc_or_modify_mutex);

	widget_points_set(&points, 1, tree_width - 1, 1, height - 1);
	treeview_redraw(&tab_room->treeview, &points);

	widget_points_set(&points, 1, tree_width - 1, 0, height);

	widget_points_set(&points, 0, tree_width, 0, height);
	border_redraw(&points, TB_DEFAULT, TB_DEFAULT);
}
