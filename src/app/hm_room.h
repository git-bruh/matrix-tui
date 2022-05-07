#pragma once
/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "app/room_ds.h"
#include "stb_ds.h"

struct hm_room {
	char *key;
	struct room *value;
};

struct state_rooms {
	struct hm_room *rooms;
	struct hm_room *orphaned_rooms;
};

static inline struct room *
rooms_get_room(struct hm_room *rooms, char *key) {
	ptrdiff_t tmp = 0;
	return shget_ts(rooms, key, tmp);
}

static inline ptrdiff_t
rooms_get_index(struct hm_room *rooms, char *key) {
	ptrdiff_t tmp = 0;
	return shgeti_ts(rooms, key, tmp);
}
