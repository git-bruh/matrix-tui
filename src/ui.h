#include "room_ds.h"
#include "widgets.h"

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
