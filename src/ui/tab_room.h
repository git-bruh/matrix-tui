/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "ui/ui.h"

struct state;

void
tab_room_finish(struct tab_room *tab_room);

int
tab_room_init(struct tab_room *tab_room);

void
tab_room_add_room(
  struct tab_room *tab_room, size_t index, struct hm_room *room);

void
tab_room_reset_rooms(struct tab_room *tab_room, struct state *state);
