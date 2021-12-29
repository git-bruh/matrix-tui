#ifndef UI_H
#define UI_H
#include "login_form.h"
#include "widgets.h"

enum hsl_constants {
	/* HUE = Generated from string. */
	HUE_MAX = 360,
	/* These values look good in my terminal :P */
	SATURATION = 50,
	LIGHTNESS = 60,
};

enum colors {
	COLOR_RED = 0x01,
	COLOR_BLUE = 0x04,
};

struct members_map {
	char *key;		 /* Full mxid, @user:domain.tld */
	uint32_t *value; /* Rendered username, either stripped MXID odisplayname
					  * set in state. */
};

struct room;

struct tab_room {
	enum tab_room_widget {
		TAB_ROOM_INPUT = 0,
		TAB_ROOM_MESSAGE,
		TAB_ROOM_MEMBERS,
	} widget;
	size_t already_consumed;
	const char *id;
	struct input *input;
	struct room *room;
};

uintattr_t
hsl_to_rgb(double h, double s, double l);
uint32_t *
buf_to_uint32_t(const char *buf);
uint32_t *
mxid_to_uint32_t(char *mxid);
uintattr_t
str_attr(const char *str);

void
tab_room_redraw(struct tab_room *room);
void
tab_login_redraw(struct form *form, const char *error);
#endif /* !UI_H */
