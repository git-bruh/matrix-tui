#pragma once
/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "widgets.h"

enum field { FIELD_MXID = 0, FIELD_PASSWORD, FIELD_HOMESERVER, FIELD_MAX };
enum field_buttons {
	FIELD_BUTTON_LOGIN,
	FIELD_BUTTON_REGISTER,
	FIELD_BUTTON_MAX
};

enum {
	FORM_COLS_PER_FIELD = 3,
	/* +3 for gap and buttons + 1 */
	FORM_HEIGHT = (FORM_COLS_PER_FIELD * FIELD_MAX) + 3
};

enum form_event { FORM_UP = 0, FORM_DOWN };

struct form {
	uintattr_t highlighted_fg;
	bool button_is_selected;
	enum field_buttons current_button;
	struct input fields[FIELD_MAX];
	size_t current_field;
};

int
form_init(struct form *form, uintattr_t highlighted_fg);
void
form_finish(struct form *form);
enum widget_error
form_handle_event(struct form *form, enum form_event event);
struct input *
form_current_input(struct form *form);
void
form_redraw(struct form *form, struct widget_points *points);
