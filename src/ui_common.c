/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "widgets.h"

uint32_t
widget_uc_sanitize(uint32_t uc, int *width) {
	int tmp_width = wcwidth((wchar_t) uc);

	switch (uc) {
	case '\n':
		*width = 0;
		return uc;
	case '\t':
		*width = 1;
		return ' ';
	default:
		if (tmp_width <= 0 || tmp_width > WIDGET_CH_MAX) {
			*width = 1;
			return '?';
		}

		*width = tmp_width;
		return uc;
	}
}

bool
widget_points_in_bounds(const struct widget_points *points, int x, int y) {
	return (
	  x >= points->x1 && x < points->x2 && y >= points->y1 && y < points->y2);
}

bool
widget_should_forcebreak(int width) {
	return width == 0;
}

bool
widget_should_scroll(int x, int width, int max_width) {
	return (x >= (max_width - width) || (widget_should_forcebreak(width)));
}

/* Returns the number of times y was advanced. */
int
widget_adjust_xy(
  int width, const struct widget_points *points, int *x, int *y) {
	int original_y = *y;

	if ((widget_should_scroll(*x, width, points->x2))) {
		*x = points->x1;
		(*y)++;
	}

	/* Newline, already scrolled. */
	if ((widget_should_forcebreak(width))) {
		return *y - original_y;
	}

	*x += width;

	/* We must accomodate for another character to move the cursor to the next
	 * line, which prevents us from adding an unreachable character. */
	if ((widget_should_scroll(*x, WIDGET_CH_MAX, points->x2))) {
		*x = points->x1;
		(*y)++;
	}

	return *y - original_y;
}

int
widget_print_str(
  int x, int y, int max_x, uintattr_t fg, uintattr_t bg, const char *str) {
	if (!str) {
		return x;
	}

	uint32_t uc;
	int width = 0;
	int original = x;

	while (!(widget_should_scroll(x, WIDGET_CH_MAX, max_x)) && *str) {
		str += tb_utf8_char_to_unicode(&uc, str);
		uc = widget_uc_sanitize(uc, &width);
		tb_set_cell(x, y, uc, fg, bg);
		x += width;
	}

	return x - original;
}
