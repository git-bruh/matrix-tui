/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "app/queue_callbacks.h"
#include "app/state.h"
#include "matrix.h"
#include "ui/login_form.h"
#include "ui/tab_room.h"

#include <assert.h>

static enum widget_error
handle_tree(
  struct tab_room *tab_room, struct tb_event *event, struct state *state) {
	assert(tab_room);
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
				tab_room_reset_rooms(tab_room, state);
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
	case TB_KEY_MOUSE_RELEASE:
		return message_buffer_handle_event(
		  buf, MESSAGE_BUFFER_SELECT, event->x, event->y);
		break;
	default:
		break;
	}

	return WIDGET_NOOP;
}

enum widget_error
handle_tab_room(
  struct state *state, struct tab_room *tab_room, struct tb_event *event) {
	enum widget_error ret = WIDGET_NOOP;

	if (!tab_room->selected_room) {
		return ret;
	}

	struct room *room = tab_room->selected_room->value;

	if (event->type == TB_EVENT_RESIZE) {
		return WIDGET_REDRAW;
	}

	if (event->type == TB_EVENT_MOUSE) {
		pthread_mutex_lock(&room->realloc_or_modify_mutex);
		ret = handle_message_buffer(&room->buffer, event);
		pthread_mutex_unlock(&room->realloc_or_modify_mutex);
	} else {
		bool enter_pressed = false;

		switch (tab_room->widget) {
		case TAB_ROOM_TREE:
			ret = handle_tree(tab_room, event, state);
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
				ret = 0;
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
