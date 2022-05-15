/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "app/hm_room.h"
#include "ui/ui.h"

void
tab_room_finish(struct tab_room *tab_room);

int
tab_room_init(struct tab_room *tab_room);

void
tab_room_reset_rooms(
  struct tab_room *tab_room, struct state_rooms *state_rooms);
