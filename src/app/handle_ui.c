/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "app/state.h"

#include <assert.h>

static enum widget_error
handle_tree(struct tab_room *tab_room, struct state_rooms *state_rooms,
  struct tb_event *event) {
	assert(tab_room);
	assert(state_rooms);
	assert(event);

	switch (event->key) {
	case TB_KEY_ENTER:
		/* Selected and not a root node (Invites/Spaces/DMs/Rooms/...) */
		if (tab_room->treeview.selected
			&& tab_room->treeview.selected->parent->parent) {
			tab_room->selected_room = tab_room->treeview.selected->data;

			if (tab_room->selected_room->value->info.is_space) {
				arrput(tab_room->path, tab_room->selected_room->key);
				tab_room->selected_room = NULL;

				/* Reset to the first room in the space. */
				tab_room_reset_rooms(tab_room, state_rooms);
			}

			return WIDGET_REDRAW;
		}

		break;
	case TB_KEY_ARROW_UP:
		return treeview_event(&tab_room->treeview, TREEVIEW_UP);
	case TB_KEY_ARROW_DOWN:
		return treeview_event(&tab_room->treeview, TREEVIEW_DOWN);
	default:
		if (event->ch == ' ') {
			return treeview_event(&tab_room->treeview, TREEVIEW_EXPAND);
		}

		break;
	}

	return WIDGET_NOOP;
}

static enum widget_error
handle_input(struct input *input, struct tb_event *event, bool *enter_pressed) {
	assert(input);
	assert(event);

	if (!event->key && event->ch) {
		return input_handle_event(input, INPUT_ADD, event->ch);
	}

	bool mod = (event->mod & TB_MOD_SHIFT) == TB_MOD_SHIFT;
	bool mod_enter = (event->mod & TB_MOD_ALT) == TB_MOD_ALT;

	switch (event->key) {
	case TB_KEY_ENTER:
		if (mod_enter) {
			return input_handle_event(input, INPUT_ADD, '\n');
		}

		if (enter_pressed) {
			*enter_pressed = true;
		}
		break;
	case TB_KEY_BACKSPACE:
	case TB_KEY_BACKSPACE2:
		return input_handle_event(
		  input, mod ? INPUT_DELETE_WORD : INPUT_DELETE);
	case TB_KEY_ARROW_RIGHT:
		return input_handle_event(input, mod ? INPUT_RIGHT_WORD : INPUT_RIGHT);
	case TB_KEY_ARROW_LEFT:
		return input_handle_event(input, mod ? INPUT_LEFT_WORD : INPUT_LEFT);
	default:
		break;
	}

	return WIDGET_NOOP;
}

static enum widget_error
handle_message_buffer(struct message_buffer *buf, struct tb_event *event) {
	assert(event->type == TB_EVENT_MOUSE);

	switch (event->key) {
	case TB_KEY_MOUSE_WHEEL_UP:
		if ((message_buffer_handle_event(buf, MESSAGE_BUFFER_UP))
			== WIDGET_NOOP) {
			/* TODO paginate(); */
		} else {
			return WIDGET_REDRAW;
		}
		break;
	case TB_KEY_MOUSE_WHEEL_DOWN:
		return message_buffer_handle_event(buf, MESSAGE_BUFFER_DOWN);
	case TB_KEY_MOUSE_LEFT:
		return message_buffer_handle_event(
		  buf, MESSAGE_BUFFER_SELECT, event->x, event->y);
		break;
	default:
		break;
	}

	return WIDGET_NOOP;
}

static enum tab_room_widget
tab_room_find_widget(struct tab_room *tab_room, int x, int y) {
	struct widget_points points[TAB_ROOM_MAX] = {0};
	tab_room_get_points(tab_room, points);

	for (enum tab_room_widget widget = 0; widget < TAB_ROOM_MAX; widget++) {
		if (widget_points_in_bounds(&points[widget], x, y)) {
			return widget;
		}
	}

	return TAB_ROOM_MAX;
}

enum widget_error
handle_tab_room(
  struct state *state, struct tab_room *tab_room, struct tb_event *event) {
	assert(state);
	assert(tab_room);
	assert(event);

	enum widget_error ret = WIDGET_NOOP;

	if (event->type == TB_EVENT_RESIZE) {
		return WIDGET_REDRAW;
	}

	/* Find the new widget to switch to, forwarding the event to the current
	 * widget in case of no change. */
	if (event->type == TB_EVENT_MOUSE && event->key == TB_KEY_MOUSE_LEFT) {
		enum tab_room_widget new_widget
		  = tab_room_find_widget(tab_room, event->x, event->y);

		if (new_widget == TAB_ROOM_MAX) {
			return ret;
		}

		if (new_widget != tab_room->widget) {
			tab_room->widget = new_widget;
			return WIDGET_REDRAW;
		}
	}

	if (event->type == TB_EVENT_MOUSE) {
		if (tab_room->widget != TAB_ROOM_MESSAGE_BUFFER
			|| !tab_room->selected_room) {
			return WIDGET_NOOP;
		}

		struct room *room = tab_room->selected_room->value;

		pthread_mutex_lock(&room->realloc_or_modify_mutex);
		ret = handle_message_buffer(&room->buffer, event);
		pthread_mutex_unlock(&room->realloc_or_modify_mutex);
	} else {
		bool enter_pressed = false;

		switch (tab_room->widget) {
		case TAB_ROOM_TREE:
			ret = handle_tree(tab_room, &state->state_rooms, event);
			break;
		case TAB_ROOM_INPUT:
			ret = handle_input(&tab_room->input, event, &enter_pressed);

			if (enter_pressed) {
				char *buf = input_buf(&tab_room->input);

				/* Empty field. */
				if (!buf) {
					break;
				}

				struct sent_message *message = malloc(sizeof(*message));

				assert(tab_room->selected_room);

				*message = (struct sent_message) {
				  .has_reply = false,
				  .reply_index = 0,
				  .buf = buf,
				  .room_id = tab_room->selected_room->key,
				};

				lock_and_push(
				  state, queue_item_alloc(QUEUE_ITEM_MESSAGE, message));

				ret = input_handle_event(&tab_room->input, INPUT_CLEAR);
			}
			break;
		case TAB_ROOM_MESSAGE_BUFFER:
			break;
		default:
			assert(0);
		}
	}

	return ret;
}

static int
login_with_info(struct state *state, struct form *form) {
	assert(state);

	int ret = -1;

	char *username = input_buf(&form->fields[FIELD_MXID]);
	char *password = input_buf(&form->fields[FIELD_PASSWORD]);
	char *homeserver = input_buf(&form->fields[FIELD_HOMESERVER]);

	bool password_sent_to_queue = false;

	if (username && password && homeserver) {
		if (state->matrix) {
			if ((matrix_set_mxid_homeserver(
				  state->matrix, username, homeserver))
				== 0) {
				ret = 0;
			}
		} else {
			ret = (state->matrix = matrix_alloc(username, homeserver, state))
				  ? 0
				  : -1;
		}

		if (ret == 0) {
			if (lock_and_push(
				  state, queue_item_alloc(QUEUE_ITEM_LOGIN, password))
				== 0) {
				password_sent_to_queue = true;
			} else {
				password = NULL; /* Freed */
			}
		}
	}

	free(username);
	free(homeserver);

	if (!password_sent_to_queue) {
		free(password);
	}

	return ret;
}

enum widget_error
handle_tab_login(
  struct state *state, struct tab_login *login, struct tb_event *event) {
	assert(login);
	assert(event);

	if (event->type == TB_EVENT_RESIZE) {
		return WIDGET_REDRAW;
	}

	if (login->logging_in) {
		return WIDGET_NOOP;
	}

	switch (event->key) {
	case TB_KEY_ARROW_UP:
		return form_handle_event(&login->form, FORM_UP);
	case TB_KEY_ARROW_DOWN:
		return form_handle_event(&login->form, FORM_DOWN);
	case TB_KEY_ENTER:
		if (!login->form.button_is_selected) {
			return WIDGET_NOOP;
		}

		if ((login_with_info(state, &login->form)) == 0) {
			login->error = NULL;
			login->logging_in = true;
		} else {
			login->error = "Invalid Information";
		}

		return WIDGET_REDRAW;
	default:
		{
			struct input *input = form_current_input(&login->form);

			if (input) {
				return handle_input(input, event, NULL);
			}

			return WIDGET_NOOP;
		}
	}
}
