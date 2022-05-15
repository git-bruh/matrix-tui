/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "ui/tab_room.h"

#include "app/room_ds.h"
#include "stb_ds.h"

#include <assert.h>

const char *const root_node_str[NODE_MAX] = {
  [NODE_INVITES] = "Invites",
  [NODE_SPACES] = "Spaces",
  [NODE_DMS] = "DMs",
  [NODE_ROOMS] = "Rooms",
};

static void
node_draw_cb(void *data, struct widget_points *points, bool is_selected) {
	assert(data);
	assert(points);

	widget_print_str(points->x1, points->y1, points->x2,
	  is_selected ? TB_REVERSE : TB_DEFAULT, TB_DEFAULT, data);
}

static void
room_draw_cb(void *data, struct widget_points *points, bool is_selected) {
	assert(data);
	assert(points);

	struct room *room = ((struct hm_room *) data)->value;

	const char *str = room->info.name ? room->info.name : "Empty Room";

	widget_print_str(points->x1, points->y1, points->x2,
	  is_selected ? TB_REVERSE : TB_DEFAULT, TB_DEFAULT, str);
}

void
tab_room_finish(struct tab_room *tab_room) {
	if (tab_room) {
		input_finish(&tab_room->input);
		treeview_node_finish(&tab_room->treeview.root);
		arrfree(tab_room->room_nodes);
		arrfree(tab_room->path);
		memset(tab_room, 0, sizeof(*tab_room));
	}
}

int
tab_room_init(struct tab_room *tab_room) {
	assert(tab_room);

	*tab_room = (struct tab_room) {.widget = TAB_ROOM_TREE};

	int ret = input_init(&tab_room->input, TB_DEFAULT, false);
	assert(ret == 0);

	ret = treeview_init(&tab_room->treeview);
	assert(ret == 0);

	for (size_t i = 0; i < NODE_MAX; i++) {
		treeview_node_init(
		  &tab_room->root_nodes[i], noconst(root_node_str[i]), node_draw_cb);
		treeview_node_add_child(
		  &tab_room->treeview.root, &tab_room->root_nodes[i]);
	}

	tab_room->treeview.selected = tab_room->treeview.root.nodes[0];

	return 0;
}

static void
tab_room_add_room(
  struct tab_room *tab_room, size_t index, struct hm_room *room) {
	treeview_node_init(&tab_room->room_nodes[index], room, room_draw_cb);

	treeview_node_add_child(
	  &tab_room
		 ->root_nodes[room->value->info.is_space ? NODE_SPACES : NODE_ROOMS],
	  &tab_room->room_nodes[index]);

	if (tab_room->selected_room
		&& room->value == tab_room->selected_room->value) {
		enum widget_error ret = treeview_event(
		  &tab_room->treeview, TREEVIEW_JUMP, &tab_room->room_nodes[index]);
		assert(ret == WIDGET_REDRAW);
	}
}

/* We're lazy so we just reset the whole tree view on any space related change
 * such as the removal/addition of a room from/to a space. This is much less
 * error prone than manually managing the nodes, and isn't really that
 * inefficient if you consider how infrequently room changes occur. */
void
tab_room_reset_rooms(
  struct tab_room *tab_room, struct state_rooms *state_rooms) {
	assert(tab_room);

	/* Reset all indices and pointers so that the treeview indices aren't messed
	 * up when we TREEVIEW_JUMP to the final node in certain cases. */
	tab_room->treeview.selected = NULL;
	tab_room->treeview.root.index = 0;

	for (size_t i = 0; i < NODE_MAX; i++) {
		arrsetlen(tab_room->treeview.root.nodes[i]->nodes, 0);
		tab_room->treeview.root.nodes[i]->index = 0;
	}

	if (arrlenu(tab_room->path) > 0) {
		/* TODO verify path. */
		struct room *space = rooms_get_room(
		  state_rooms->rooms, tab_room->path[arrlenu(tab_room->path) - 1]);
		assert(space);

		/* A child space might have more rooms than root orphans. */
		arrsetlen(tab_room->room_nodes, shlenu(space->children));

		for (size_t i = 0, skipped = 0, len = shlenu(space->children); i < len;
			 i++) {
			ptrdiff_t child_index
			  = rooms_get_index(state_rooms->rooms, space->children[i].key);

			/* Child room not joined yet. */
			if (child_index == -1) {
				skipped++;
				continue;
			}

			tab_room_add_room(
			  tab_room, i - skipped, &state_rooms->rooms[child_index]);
		}
	} else {
		arrsetlen(tab_room->room_nodes, shlenu(state_rooms->orphaned_rooms));

		for (size_t i = 0, len = shlenu(state_rooms->orphaned_rooms); i < len;
			 i++) {
			ptrdiff_t index
			  = shgeti(state_rooms->rooms, state_rooms->orphaned_rooms[i].key);
			assert(index != -1);

			tab_room_add_room(tab_room, i, &state_rooms->rooms[index]);
		}
	}

	/* Found current room. */
	if (tab_room->treeview.selected) {
		return;
	}

	/* Find the first non-empty node and choose it's first room as the
	 * selected one. */
	for (size_t i = 0; i < NODE_MAX; i++) {
		struct treeview_node **nodes = tab_room->treeview.root.nodes[i]->nodes;

		if (arrlenu(nodes) > 0) {
			enum widget_error ret
			  = treeview_event(&tab_room->treeview, TREEVIEW_JUMP, nodes[0]);
			assert(ret == WIDGET_REDRAW);

			tab_room->selected_room = tab_room->treeview.selected->data;
			return;
		}
	}

	tab_room->selected_room = NULL;
}
