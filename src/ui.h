#ifndef UI_H
#define UI_H
/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "login_form.h"
#include "state.h"
#include "widgets.h"

#undef CTRL
#define CTRL(x) ((x) &037)

enum hsl_constants {
	/* HUE = Generated from string. */
	HUE_MAX = 360,
	/* These values look good in my terminal :P */
	SATURATION = 50,
	LIGHTNESS = 60,
};

enum colors { COLOR_RED = 0x01, COLOR_BLUE = 0x04, COLOR_BLACK = 0x10 };

struct members_map {
	char *key; /* Full mxid, @user:domain.tld */
	/* Array of usernames receive throughout various sync responses. Either
	 * stripped MXID or displayname from state events. */
	uint32_t **value;
};

struct room;

struct tab_room {
	enum tab_room_widget {
		TAB_ROOM_INPUT = 0,
		TAB_ROOM_TREE,
		/* TODO */
		TAB_ROOM_MESSAGE_BUFFER,
		TAB_ROOM_MEMBERS,
	} widget;
	struct input input;
	struct treeview_node root_nodes[NODE_MAX];
	struct treeview treeview;
	struct hm_room *selected_room;
	struct treeview_node *room_nodes;
	char **path; /* Path to follow to reach the current parent space. */
};

struct tab_login {
	bool logging_in;
	struct form form;
	const char *error;
};

uintattr_t
hsl_to_rgb(double h, double s, double l);
/* len == 0 means calculate strlen() */
uint32_t *
buf_to_uint32_t(const char *buf, size_t len);
uint32_t *
mxid_to_uint32_t(const char *mxid);
uintattr_t
str_attr(const char *str);

void
tab_room_get_buffer_points(struct widget_points *points);
void
tab_room_redraw(struct tab_room *room);
void
tab_login_redraw(struct tab_login *login);
#endif /* !UI_H */
