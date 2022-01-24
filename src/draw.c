/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "message_buffer.h"
#include "room_ds.h"
#include "ui.h"
#include "widgets.h"

#include <assert.h>
#include <math.h>

enum {
	BAR_HEIGHT = 1,
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
	int padding_y = widget_pad_center(FORM_HEIGHT, height - BAR_HEIGHT);

	widget_points_set(
	  &points, 0, width, BAR_HEIGHT, (BAR_HEIGHT + padding_y) - FORM_ART_GAP);
	art_redraw(&points);

	widget_points_set(&points, padding_x, width - padding_x,
	  BAR_HEIGHT + padding_y, height - padding_y);
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
	  points, part_percent(width, TAB_ROOM_TREE_PERCENT) + 1, width, 0, 0);
}

void
tab_room_redraw(struct tab_room *tab_room) {
	assert(tab_room);

	struct room *room = tab_room->current_room.room;

	if (!room) {
		return;
	}

	int height = tb_height();
	int width = tb_width();

	struct widget_points points = {0};

	int tree_width = part_percent(width, TAB_ROOM_TREE_PERCENT);

	{
		const char *name_or_id = "Default";

		bool other_spaces_have_events = true;
		uintattr_t fg = str_attr(name_or_id);

		if (other_spaces_have_events) {
			fg |= TB_REVERSE;
		}

		int x = widget_print_str(0, 0, tree_width, fg, TB_DEFAULT, name_or_id);

		if (other_spaces_have_events) {
			/* Fill gap between name and bar. */
			for (; x < tree_width; x++) {
				tb_set_cell(x, 0, ' ', fg, TB_DEFAULT);
			}
		}
	}

	/* + 1 for divider bar. */
	widget_points_set(
	  &points, tree_width + 1, width, height - INPUT_HEIGHT, height);

	int input_rows = 0;
	input_redraw(tab_room->input, &points, &input_rows);

	widget_points_set(
	  &points, tree_width + 1, width, BAR_HEIGHT, height - input_rows);

	pthread_mutex_lock(&room->realloc_or_modify_mutex);
	message_buffer_redraw(&room->buffer, room->members, &points);
	pthread_mutex_unlock(&room->realloc_or_modify_mutex);

	/* - 1 for divider bar. */
	widget_points_set(&points, 0, tree_width - 1, BAR_HEIGHT, height);

	pthread_mutex_lock(tab_room->rooms_mutex);
	treeview_redraw(tab_room->tree, &points);
	pthread_mutex_unlock(tab_room->rooms_mutex);

	for (int y = 0; y < height; y++) {
		widget_print_str(tree_width, y, width, TB_DEFAULT, TB_DEFAULT, "│");
	}
}
