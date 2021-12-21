#include "login_form.h"

#include <assert.h>

enum {
	BORDER_NORMAL = 0,
	BORDER_CORNER_LEFT,
	BORDER_CORNER_RIGHT,
	BORDER_CORNER_LEFT_BOTTOM,
	BORDER_CORNER_RIGHT_BOTTOM,
	BORDER_VERTICAL,
	BORDER_MAX
};

static const char *borders[BORDER_MAX] = {
  [BORDER_NORMAL] = "─",
  [BORDER_CORNER_LEFT] = "┌",
  [BORDER_CORNER_RIGHT] = "┐",
  [BORDER_CORNER_LEFT_BOTTOM] = "└",
  [BORDER_CORNER_RIGHT_BOTTOM] = "┘",
  [BORDER_VERTICAL] = "│",
};

static const char *field_names[FIELD_MAX] = {
  [FIELD_MXID] = "Username",
  [FIELD_PASSWORD] = "Password",
  [FIELD_HOMESERVER] = "Homeserver",
};

static const char *button_names[FIELD_BUTTON_MAX]
  = {[FIELD_BUTTON_LOGIN] = "Login", [FIELD_BUTTON_REGISTER] = "Register"};

int
form_init(struct form *form, uintattr_t highlighted_fg) {
	assert(form);

	*form = (struct form) {.highlighted_fg = highlighted_fg};

	for (size_t i = 0; i < FIELD_MAX; i++) {
		if ((input_init(&form->fields[i], TB_DEFAULT, true)) == -1) {
			form_finish(form);
			return -1;
		}
	}

	return 0;
}

void
form_finish(struct form *form) {
	if (form) {
		for (size_t i = 0; i < FIELD_MAX; i++) {
			input_finish(&form->fields[i]);
		}

		memset(form, 0, sizeof(*form));
	}
}

enum widget_error
form_handle_event(struct form *form, enum form_event event) {
	assert(form);

	switch (event) {
	case FORM_UP:
		if (form->current_button > 0) {
			form->current_button--;
			return WIDGET_REDRAW;
		}

		if (form->button_is_selected) {
			form->button_is_selected = false;
			return WIDGET_REDRAW;
		}

		if (form->current_field > 0) {
			form->current_field--;
			return WIDGET_REDRAW;
		}
		break;
	case FORM_DOWN:
		if ((form->current_field + 1) < FIELD_MAX) {
			form->current_field++;
			return WIDGET_REDRAW;
		} else {
			if (!form->button_is_selected) {
				form->button_is_selected = true;
				return WIDGET_REDRAW;
			} else if ((form->current_button + 1) < FIELD_BUTTON_MAX) {
				form->current_button++;
				return WIDGET_REDRAW;
			}
		}
		break;
	default:
		assert(0);
	}

	return WIDGET_NOOP;
}

struct input *
form_current_input(struct form *form) {
	assert(form);
	return !form->button_is_selected ? &form->fields[form->current_field]
									 : NULL;
}

static void
padding_redraw(int x1, int x2, int y, const char *title, uintattr_t fg) {
	bool top = !!title;

	const char *start
	  = top ? borders[BORDER_CORNER_LEFT] : borders[BORDER_CORNER_LEFT_BOTTOM];
	const char *end = top ? borders[BORDER_CORNER_RIGHT]
						  : borders[BORDER_CORNER_RIGHT_BOTTOM];

	int x = x1;

	widget_print_str(x++, y, x2, fg, TB_DEFAULT, start);

	if (top) {
		int padding = widget_pad_center(widget_str_width(title), x2 - x1);

		/* Center relative to the whole screen. */
		for (; (x - x1) < padding; x++) {
			widget_print_str(x, y, x2, fg, TB_DEFAULT, borders[BORDER_NORMAL]);
		}

		x += widget_print_str(x, y, x2, TB_DEFAULT, TB_DEFAULT, title);
	}

	for (; x < (x2 - 1); x++) {
		widget_print_str(x, y, x2, fg, TB_DEFAULT, borders[BORDER_NORMAL]);
	}

	widget_print_str(x++, y, x2, fg, TB_DEFAULT, end);
}

static void
field_border_redraw(
  struct form *form, struct widget_points *points, enum field field) {
	assert(form);
	assert(points);

	int y = points->y1 + ((int) field * FORM_COLS_PER_FIELD);
	int x = points->x1;

	uintattr_t fg
	  = (!form->button_is_selected && field == (enum field) form->current_field)
		? form->highlighted_fg
		: TB_DEFAULT;

	if (y >= points->y2) {
		return;
	}

	padding_redraw(x, points->x2, y, field_names[field], fg);

	if (++y >= points->y2) {
		return;
	}

	widget_print_str(
	  x, y, points->x2, fg, TB_DEFAULT, borders[BORDER_VERTICAL]);
	widget_print_str(
	  points->x2 - 1, y, points->x2, fg, TB_DEFAULT, borders[BORDER_VERTICAL]);

	if (++y >= points->y2) {
		return;
	}

	padding_redraw(x, points->x2, y, NULL, fg);
}

void
form_redraw(struct form *form, struct widget_points *points) {
	assert(form);
	assert(points);

	struct widget_points points_new = {0};

	enum field start = 0;
	int height = points->y2 - points->y1;

	if ((int) form->current_field > height) {
		start = (enum field) form->current_field;
	}

	int y_selected = -1;
	int lines = 0;

	int y = FORM_COLS_PER_FIELD - 1;

	for (enum field i = start; i < FIELD_MAX; i++, y += FORM_COLS_PER_FIELD) {
		field_border_redraw(form, points, i);

		if (i == form->current_field) {
			y_selected = points->y1 + y;
			continue;
		}

		if ((points->y1 + y) > points->y2) {
			break;
		}

		widget_points_set(&points_new, points->x1 + 1, points->x2 - 1,
		  (points->y1 + y) - 1, points->y1 + y);
		input_redraw(&form->fields[i], &points_new, &lines);

		assert(lines == 1);
	}

	assert(y_selected != -1);

	widget_points_set(
	  &points_new, points->x1 + 1, points->x2 - 1, y_selected - 1, y_selected);
	input_redraw(&form->fields[form->current_field], &points_new, &lines);

	int x_split_per_button = (points->x2 - points->x1) / FIELD_BUTTON_MAX;

	int y_button = points->y1 + (FORM_HEIGHT - 2);

	if (form->button_is_selected) {
		tb_hide_cursor();
	}

	if (y_button >= points->y2) {
		return;
	}

	for (enum field_buttons button = 0; button < FIELD_BUTTON_MAX; button++) {
		int x_padding = widget_pad_center(
		  widget_str_width(button_names[button]), x_split_per_button);
		widget_print_str(
		  points->x1 + ((int) button * x_split_per_button) + x_padding,
		  y_button, points->x2,
		  (form->button_is_selected && button == form->current_button)
			? form->highlighted_fg
			: TB_DEFAULT,
		  TB_DEFAULT, button_names[button]);
	}

	assert(lines == 1);
}
