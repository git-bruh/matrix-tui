#include "message_buffer.h"
#include "room_ds.h"
#include "ui.h"
#include "widgets.h"

#include <assert.h>

enum {
	BAR_HEIGHT = 1,
	INPUT_HEIGHT = 5,
	FORM_WIDTH = 68,
	FORM_ART_GAP = 2,
};

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
}

void
tab_room_get_buffer_points(struct widget_points *points) {
	assert(points);
	widget_points_set(points, 0, tb_width(), 0, 0);
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

	pthread_mutex_lock(tab_room->rooms_mutex);
	int x = 0;

	for (size_t i = 0, len = shlenu(tab_room->rooms); i < len; i++) {
		const char *name_or_id = tab_room->rooms[i].value->info->name
								 ? tab_room->rooms[i].value->info->name
								 : tab_room->rooms[i].key;
		assert(name_or_id);

		uintattr_t fg = str_attr(name_or_id) | TB_REVERSE;

		x += widget_print_str(x, 0, width, fg, TB_DEFAULT, " ");
		x += widget_print_str(x, 0, width,
		  fg | (i == tab_room->current_room.index ? TB_UNDERLINE : 0),
		  TB_DEFAULT, name_or_id);
		x += widget_print_str(x, 0, width, fg, TB_DEFAULT, " ");
		/* TODO > at end, highlight if further items have events. */
	}
	pthread_mutex_unlock(tab_room->rooms_mutex);

	widget_points_set(&points, 0, width, height - INPUT_HEIGHT, height);

	int input_rows = 0;
	input_redraw(tab_room->input, &points, &input_rows);

	widget_points_set(&points, 0, width, BAR_HEIGHT, height - input_rows);

	pthread_mutex_lock(&room->realloc_or_modify_mutex);
	message_buffer_redraw(&room->buffer, room->members, &points);
	pthread_mutex_unlock(&room->realloc_or_modify_mutex);
}
