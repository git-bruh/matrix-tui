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
	FORM_WIDTH = 68,
	FORM_ART_GAP = 2,
	TAB_ROOM_TREE_PERCENT = 20,
	BORDER_HIGHLIGHT_FG = COLOR_BLUE,
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

static void
adjust_inside_border(struct widget_points *points) {
	assert(points);

	widget_points_set(
	  points, points->x1 + 1, points->x2 - 1, points->y1 + 1, points->y2 - 1);
}

/* nullify adjust_inside_border */
static void
adjust_outside_border(struct widget_points *points) {
	widget_points_set(
	  points, points->x1 - 1, points->x2 + 1, points->y1 - 1, points->y2 + 1);
}

static void
border_highlight(struct widget_points *points, bool highlight) {
	struct widget_points copy = *points;

	adjust_outside_border(&copy);
	border_redraw(
	  &copy, highlight ? BORDER_HIGHLIGHT_FG : TB_DEFAULT, TB_DEFAULT);
}

void
tab_room_get_points(
  struct tab_room *tab_room, struct widget_points points[TAB_ROOM_MAX]) {
	const int input_border_px = 2; /* Border above and below the field. */

	int height = tb_height();
	int width = tb_width();

	widget_points_set(&points[TAB_ROOM_TREE], 0,
	  part_percent(width, TAB_ROOM_TREE_PERCENT), 0, height);

	int input_rows = -1;
	widget_points_set(&points[TAB_ROOM_INPUT], points[TAB_ROOM_TREE].x2, width,
	  height - INPUT_HEIGHT - input_border_px, height);
	adjust_inside_border(
	  &points[TAB_ROOM_INPUT]); /* Do a dry run of drawing the input field to
								   get rows. */
	input_redraw(&tab_room->input, &points[TAB_ROOM_INPUT], &input_rows, true);
	assert(input_rows >= 0);

	if (input_rows == 0) {
		input_rows = 1;
	}

	widget_points_set(&points[TAB_ROOM_INPUT], points[TAB_ROOM_TREE].x2, width,
	  height - input_rows - input_border_px, height);
	widget_points_set(&points[TAB_ROOM_MESSAGE_BUFFER],
	  points[TAB_ROOM_TREE].x2, width, 0, points[TAB_ROOM_INPUT].y1);

	/* We include borders in the above coordinates for easier organization.
	 * Provide non-border points. */
	for (enum tab_room_widget widget = 0; widget < TAB_ROOM_MAX; widget++) {
		adjust_inside_border(&points[widget]);
	}
}

void
tab_room_redraw(struct tab_room *tab_room) {
	assert(tab_room);

	struct widget_points points[TAB_ROOM_MAX] = {0};
	tab_room_get_points(tab_room, points);

	for (enum tab_room_widget widget = 0; widget < TAB_ROOM_MAX; widget++) {
		border_highlight(&points[widget], widget == tab_room->widget);

		switch (widget) {
		case TAB_ROOM_TREE:
			treeview_redraw(&tab_room->treeview, &points[widget]);
			break;
		case TAB_ROOM_INPUT:
			/* Don't pass input_rows as we don't neex it here. */
			input_redraw(&tab_room->input, &points[widget], &(int) {0}, false);
			break;
		case TAB_ROOM_MESSAGE_BUFFER:
			if (tab_room->selected_room) {
				struct room *room = tab_room->selected_room->value;

				pthread_mutex_lock(&room->realloc_or_modify_mutex);
				message_buffer_redraw(&room->buffer, &points[widget]);
				pthread_mutex_unlock(&room->realloc_or_modify_mutex);
			}
			break;
		default:
			assert(0);
		}
	}
}
