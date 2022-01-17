/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* Uses Nheko's cache as reference:
 * https://github.com/Nheko-Reborn/nheko/blob/master/src/Cache.cpp */
#include "cache.h"

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static const char *const db_names[DB_MAX] = {
  [DB_AUTH] = "auth",
  [DB_ROOMS] = "rooms",
  [DB_SPACE_CHILDREN] = "space_children",
};

static const char *const db_keys[DB_KEY_MAX] = {
  [DB_KEY_ACCESS_TOKEN] = "access_token",
  [DB_KEY_NEXT_BATCH] = "next_batch",
  [DB_KEY_MXID] = "mxid",
  [DB_KEY_HOMESERVER] = "homeserver",
};

static const char *const room_db_names[ROOM_DB_MAX] = {
  [ROOM_DB_EVENTS] = "events",
  [ROOM_DB_ORDER_TO_EVENTS] = "order2event",
  [ROOM_DB_EVENTS_TO_ORDER] = "event2order",
  [ROOM_DB_MEMBERS] = "members",
  [ROOM_DB_STATE] = "state",
};

/* Modifies path[] in-place but restores it. */
static int
mkdir_parents(char path[], mode_t mode) {
	for (size_t i = 0; path[i] != '\0'; i++) {
		/* Create the directory even if no trailing slash is provided. */
		if (path[i] == '/' || path[i + 1] == '\0') {
			char tmp = path[i + 1];

			path[i + 1] = '\0';
			int ret = mkdir(path, mode);
			path[i + 1] = tmp;

			if (ret == -1 && errno != EEXIST) {
				return errno;
			}
		}
	}

	return 0;
}

static int
get_dbi(enum room_db db, MDB_txn *txn, MDB_dbi *dbi, const char *room_id) {
	assert(txn);
	assert(dbi);
	assert(room_id);

	unsigned flags = MDB_CREATE;

	if (db == ROOM_DB_ORDER_TO_EVENTS) {
		flags |= MDB_INTEGERKEY;
	}

	char *room = NULL;

	if ((asprintf(&room, "%s/%s", room_id, room_db_names[db])) == -1) {
		return ENOMEM;
	}

	int ret = mdb_dbi_open(txn, room, flags, dbi);
	free(room);

	return ret;
}

static int
del_str(MDB_txn *txn, MDB_dbi dbi, const char *key, const char *data) {
	if (!key) {
		return EINVAL;
	}

	return mdb_del(txn, dbi, &(MDB_val) {strlen(key) + 1, noconst(key)},
	  (data ? &(MDB_val) {strlen(data) + 1, noconst(data)} : NULL));
}

static int
get_str(MDB_txn *txn, MDB_dbi dbi, const char *key, MDB_val *data) {
	if (!txn || !key || !data) {
		return EINVAL;
	}

	return mdb_get(txn, dbi, &(MDB_val) {strlen(key) + 1, noconst(key)}, data);
}

static char *
get_str_and_dup(MDB_txn *txn, MDB_dbi dbi, const char *key) {
	if (txn && key) {
		MDB_val data = {0};

		if ((get_str(txn, dbi, key, &data)) == MDB_SUCCESS) {
			return strndup(data.mv_data, data.mv_size);
		}
	}

	return NULL;
}

static int
put_str(MDB_txn *txn, MDB_dbi dbi, const char *key, const char *data,
  unsigned flags) {
	if (!txn || !key || !data) {
		return EINVAL;
	}

	return mdb_put(txn, dbi, &(MDB_val) {strlen(key) + 1, noconst(key)},
	  &(MDB_val) {strlen(data) + 1, noconst(data)}, flags);
}

static int
put_int(
  MDB_txn *txn, MDB_dbi dbi, uint64_t key, const char *data, unsigned flags) {
	if (!txn || !data) {
		return EINVAL;
	}

	return mdb_put(txn, dbi, &(MDB_val) {sizeof(key), &key},
	  &(MDB_val) {strlen(data) + 1, noconst(data)}, flags);
}

static int
put_str_int(
  MDB_txn *txn, MDB_dbi dbi, const char *key, uint64_t data, unsigned flags) {
	if (!txn || !key) {
		return EINVAL;
	}

	return mdb_put(txn, dbi, &(MDB_val) {strlen(key) + 1, noconst(key)},
	  &(MDB_val) {sizeof(data), &data}, flags);
}

/* TODO pool transactions. **/
static int
get_txn(struct cache *cache, unsigned flags, MDB_txn **txn) {
	assert(cache);

	return mdb_txn_begin(cache->env, NULL, flags, txn);
}

static bool
is_str(MDB_val *val) {
	assert(val);

	return (
	  val->mv_size > 0 && ((char *) val->mv_data)[val->mv_size - 1] == '\0');
}

static int
cache_rooms_next(struct cache_iterator *iterator, MDB_val *key, MDB_val *data) {
	assert(iterator);
	assert(iterator->type == CACHE_ITERATOR_ROOMS);
	assert(is_str(key));
	assert(is_str(data));

	*iterator->room_id = key->mv_data;
	return MDB_SUCCESS;
}

static int
cache_event_next(
  struct cache_iterator *iterator, MDB_val *db_index, MDB_val *id) {
	assert(iterator);
	assert(iterator->type == CACHE_ITERATOR_EVENTS);
	assert(is_str(id));

	MDB_val db_json = {0};
	int ret = mdb_get(iterator->txn, iterator->events_dbi, id, &db_json);

	if (ret != 0) {
		return ret;
	}

	assert(is_str(&db_json));

	uint64_t index = 0;
	assert(db_index->mv_size == sizeof(index));
	memcpy(&index, db_index->mv_data, sizeof(index));

	matrix_json_t *json = matrix_json_parse(db_json.mv_data, db_json.mv_size);
	assert(json);

	*iterator->event
	  = (struct cache_iterator_event) {.json = json, .index = index};

	ret = matrix_event_timeline_parse(&iterator->event->event, json);

	if (ret != 0) {
		fprintf(stderr, "Failed to parse event JSON '%s'\n",
		  (char *) db_json.mv_data);
		assert(0);

		matrix_json_delete(json);
		return EINVAL;
	}

	return 0;
}

static int
cache_member_next(
  struct cache_iterator *iterator, MDB_val *key, MDB_val *data) {
	assert(iterator);
	assert(iterator->type == CACHE_ITERATOR_MEMBER);
	assert(is_str(key));
	assert(is_str(data));

	matrix_json_t *json = matrix_json_parse(data->mv_data, data->mv_size);
	assert(json);

	struct matrix_state_event event = {0};

	int ret = matrix_event_state_parse(&event, json);

	if (ret != 0 || event.type != MATRIX_ROOM_MEMBER) {
		fprintf(
		  stderr, "Failed to parse member JSON '%s'\n", (char *) data->mv_data);
		assert(0);

		matrix_json_delete(json);
		return EINVAL;
	}

	*iterator->member = (struct cache_iterator_member) {
	  .mxid = key->mv_data,
	  .username = event.member.displayname,
	  .json = json,
	};

	return 0;
}

int
cache_iterator_rooms(
  struct cache *cache, struct cache_iterator *iterator, const char **room_id) {
	assert(cache);
	assert(iterator);

	MDB_txn *txn = NULL;
	int ret = get_txn(cache, MDB_RDONLY, &txn);

	if (ret != 0) {
		return ret;
	}

	MDB_cursor *cursor = NULL;
	ret = mdb_cursor_open(txn, cache->dbs[DB_ROOMS], &cursor);

	*iterator = (struct cache_iterator) {
	  .type = CACHE_ITERATOR_ROOMS,
	  .txn = txn,
	  .cursor = cursor,
	  .cache = cache,
	  .room_id = room_id,
	};

	if (ret != 0) {
		cache_iterator_finish(iterator);
	}

	return ret;
}

int
cache_iterator_events(struct cache *cache, struct cache_iterator *iterator,
  const char *room_id, struct cache_iterator_event *event, uint64_t end_index,
  uint64_t num_fetch) {
	assert(cache);
	assert(iterator);
	assert(room_id);
	assert(event);

	MDB_txn *txn = NULL;
	int ret = get_txn(cache, MDB_RDONLY, &txn);

	if (ret != 0) {
		return ret;
	}

	MDB_dbi events_dbi = 0;
	MDB_dbi order_dbi = 0;
	MDB_cursor *cursor = NULL;

	if ((ret = get_dbi(ROOM_DB_EVENTS, txn, &events_dbi, room_id)) == 0
		&& (ret = get_dbi(ROOM_DB_ORDER_TO_EVENTS, txn, &order_dbi, room_id))
			 == 0
		&& (ret = mdb_cursor_open(txn, order_dbi, &cursor)) == 0) {
		MDB_val start_key = {sizeof(end_index), &end_index};

		if (end_index == (uint64_t) -1) {
			MDB_val start_value = {0};
			ret = mdb_cursor_get(cursor, &start_key, &start_value, MDB_LAST);
		}

		if (ret == 0) {
			/* Position at given index. */
			ret = mdb_cursor_get(cursor, &start_key, NULL, MDB_SET);
		}
	}

	*iterator = (struct cache_iterator) {
	  .type = CACHE_ITERATOR_EVENTS,
	  .txn = txn,
	  .cursor = cursor,
	  .cache = cache,
	  .event = event,
	  .events_dbi = events_dbi,
	  .num_fetch = num_fetch,
	};

	if (ret != 0) {
		cache_iterator_finish(iterator);
	}

	return ret;
}

int
cache_iterator_member(struct cache *cache, struct cache_iterator *iterator,
  const char *room_id, struct cache_iterator_member *member) {
	assert(cache);
	assert(iterator);
	assert(room_id);
	assert(member);

	MDB_txn *txn = NULL;
	int ret = get_txn(cache, MDB_RDONLY, &txn);

	if (ret != 0) {
		return ret;
	}

	MDB_dbi dbi = 0;
	MDB_cursor *cursor = NULL;

	if ((ret = get_dbi(ROOM_DB_MEMBERS, txn, &dbi, room_id)) == 0
		&& (ret = mdb_cursor_open(txn, dbi, &cursor)) == 0) {
		/* Success */
	}

	*iterator = (struct cache_iterator) {
	  .type = CACHE_ITERATOR_MEMBER,
	  .txn = txn,
	  .cursor = cursor,
	  .cache = cache,
	  .member = member,
	};

	if (ret != 0) {
		cache_iterator_finish(iterator);
	}

	return ret;
}

int
cache_iterator_spaces(struct cache *cache, struct cache_iterator *iterator,
  struct cache_iterator_space *space) {
	assert(cache);
	assert(iterator);
	assert(space);

	MDB_txn *txn = NULL;
	int ret = get_txn(cache, MDB_RDONLY, &txn);

	if (ret != 0) {
		return ret;
	}

	MDB_cursor *cursor = NULL;

	if ((ret = mdb_cursor_open(txn, cache->dbs[DB_SPACE_CHILDREN], &cursor))
		!= MDB_SUCCESS) {
		return ret;
	}

	*iterator = (struct cache_iterator) {.type = CACHE_ITERATOR_SPACES,
	  .txn = txn,
	  .cursor = cursor,
	  .cache = cache,
	  .space = space};

	if (ret != MDB_SUCCESS) {
		cache_iterator_finish(iterator);
	}

	return ret;
}

static int
cache_iterator_space_children(struct cache *cache, MDB_cursor *cursor,
  struct cache_iterator *iterator, char **child_id) {
	assert(cache);
	assert(iterator);
	assert(child_id);

	*iterator = (struct cache_iterator) {.type = CACHE_ITERATOR_SPACE_CHILDREN,
	  .cursor = cursor,
	  .cache = cache,
	  .child_id = child_id};

	return MDB_SUCCESS;
}

void
cache_iterator_finish(struct cache_iterator *iterator) {
	if (iterator) {
		/* Cursor might be inherited from another iterator so only free it if we
		 * have a txn. */
		if (iterator->txn) {
			mdb_cursor_close(iterator->cursor);
		}

		mdb_txn_commit(iterator->txn);

		memset(iterator, 0, sizeof(*iterator));
	}
}

int
cache_init(struct cache *cache) {
	assert(cache);

	*cache = (struct cache) {0};

	const mode_t dir_perms = 0755;
	const mode_t db_perms = 0600;

	enum {
		max_dbs = 4096,
		db_size = 1 * 1024 * 1024 * 1024, /* 1 GB */
		/* Arbitrarily large map size.
		 * https://github.com/zevv/duc/issues/163 */
		map_size = db_size,
	};

	char dir[] = "/tmp/db";

	int ret = 0;

	if ((ret = mkdir_parents(dir, dir_perms)) != 0) {
		return ret;
	}

	MDB_txn *txn = NULL;

	const unsigned multiple_readonly_txn_per_thread = MDB_NOTLS;

	if ((ret = mdb_env_create(&cache->env)) == MDB_SUCCESS
		&& (ret = mdb_env_set_maxdbs(cache->env, max_dbs)) == MDB_SUCCESS
		&& (ret = mdb_env_set_mapsize(cache->env, map_size)) == MDB_SUCCESS
		&& (ret = mdb_env_open(
			  cache->env, dir, multiple_readonly_txn_per_thread, db_perms))
			 == MDB_SUCCESS
		&& (ret = get_txn(cache, 0, &txn)) == MDB_SUCCESS) {

		for (size_t i = 0; i < DB_MAX; i++) {
			unsigned flags = MDB_CREATE;

			if (i == DB_SPACE_CHILDREN) {
				flags |= MDB_DUPSORT;
			}

			if ((ret = mdb_dbi_open(txn, db_names[i], flags, &cache->dbs[i]))
				!= MDB_SUCCESS) {
				cache_finish(cache);
				mdb_txn_commit(txn);

				return ret;
			}
		}

		mdb_txn_commit(txn);
		return 0;
	}

	cache_finish(cache);
	mdb_txn_commit(txn);

	return ret;
}

void
cache_finish(struct cache *cache) {
	if (!cache) {
		return;
	}

	mdb_env_close(cache->env);
	memset(cache, 0, sizeof(*cache));
}

char *
cache_auth_get(struct cache *cache, enum auth_key key) {
	assert(cache);

	MDB_txn *txn = NULL;
	char *ret = NULL;

	if ((get_txn(cache, 0, &txn)) == MDB_SUCCESS && txn) {
		ret = get_str_and_dup(txn, cache->dbs[DB_AUTH], noconst(db_keys[key]));
	}

	mdb_txn_commit(txn);

	return ret;
}

int
cache_auth_set(struct cache *cache, enum auth_key key, char *auth) {
	assert(cache);
	assert(auth);

	MDB_txn *txn = NULL;
	int ret = get_txn(cache, 0, &txn);

	if (ret == MDB_SUCCESS) {
		ret = put_str(txn, cache->dbs[DB_AUTH], noconst(db_keys[key]), auth, 0);
	}

	mdb_txn_commit(txn);
	return ret;
}

char *
cache_room_name(struct cache *cache, MDB_txn *txn, const char *room_id) {
	if (cache && txn && room_id) {
		MDB_dbi dbi = 0;
		char *res = NULL;
		matrix_json_t *json = NULL;

		if (get_dbi(ROOM_DB_STATE, txn, &dbi, room_id) == MDB_SUCCESS) {
			char state_key[] = "m.room.name";
			char state_fallback[] = "m.room.canonical_alias";

			MDB_val value = {0};
			struct matrix_state_event sevent;

			if (((get_str(txn, dbi, state_key, &value)) == MDB_SUCCESS
				  || (get_str(txn, dbi, state_fallback, &value)) == MDB_SUCCESS)
				&& (json
					= matrix_json_parse((char *) value.mv_data, value.mv_size))
				&& (matrix_event_state_parse(&sevent, json)) == 0) {
				switch (sevent.type) {
				case MATRIX_ROOM_NAME:
					res = sevent.name.name;
					break;
				case MATRIX_ROOM_CANONICAL_ALIAS:
					res = sevent.canonical_alias.alias;
					break;
				default:
					break;
				}
			} else {
				/* TODO Check if DM. */
			}
		}

		if (res) {
			res = strdup(res);
		}

		matrix_json_delete(json);

		return res;
	}

	return NULL;
}

char *
cache_room_topic(struct cache *cache, MDB_txn *txn, const char *room_id) {
	if (cache && txn && room_id) {
		MDB_dbi dbi = 0;
		char *res = NULL;
		matrix_json_t *json = NULL;

		if (get_dbi(ROOM_DB_STATE, txn, &dbi, room_id) == MDB_SUCCESS) {
			char state_key[] = "m.room.topic";

			MDB_val value = {0};
			struct matrix_state_event sevent;

			if ((get_str(txn, dbi, state_key, &value)) == MDB_SUCCESS
				&& (json
					= matrix_json_parse((char *) value.mv_data, value.mv_size))
				&& (matrix_event_state_parse(&sevent, json)) == 0
				&& (sevent.type == MATRIX_ROOM_TOPIC)) {
				res = sevent.topic.topic;
			}
		}

		if (res) {
			res = strdup(res);
		}

		matrix_json_delete(json);

		return res;
	}

	return NULL;
}

int
cache_iterator_next(struct cache_iterator *iterator) {
	assert(iterator);

	MDB_val key = {0};
	MDB_val data = {0};

	int ret = 0;

	switch (iterator->type) {
	case CACHE_ITERATOR_ROOMS:
		if ((ret = mdb_cursor_get(iterator->cursor, &key, &data, MDB_NEXT))
			== MDB_SUCCESS) {
			return cache_rooms_next(iterator, &key, &data);
		}
		break;
	case CACHE_ITERATOR_EVENTS:
		if (iterator->num_fetch == 0) {
			ret = EINVAL;
			break;
		}

		ret = (iterator->fetched_once
				 ? (mdb_cursor_get(iterator->cursor, &key, &data, MDB_PREV))
				 : (iterator->fetched_once = true,
				   mdb_cursor_get(
					 iterator->cursor, &key, &data, MDB_GET_CURRENT)));

		if (ret == MDB_SUCCESS) {
			iterator->num_fetch--;
			ret = cache_event_next(iterator, &key, &data);
		}
		break;
	case CACHE_ITERATOR_MEMBER:
		if ((ret = mdb_cursor_get(iterator->cursor, &key, &data, MDB_NEXT))
			== MDB_SUCCESS) {
			return cache_member_next(iterator, &key, &data);
		}
		break;
	case CACHE_ITERATOR_SPACES:
		if ((ret = mdb_cursor_get(iterator->cursor, &key, &data, MDB_NEXT))
			== MDB_SUCCESS) {
			assert(is_str(&key));
			iterator->space->id = key.mv_data;

			return cache_iterator_space_children(iterator->cache,
			  iterator->cursor, &iterator->space->children_iterator,
			  &iterator->space->child_id);
		}
		break;
	case CACHE_ITERATOR_SPACE_CHILDREN:
		if ((ret = mdb_cursor_get(iterator->cursor, &key, &data,
			   iterator->child_iterated_once ? MDB_NEXT_DUP : MDB_FIRST_DUP))
			== MDB_SUCCESS) {
			assert(is_str(&data));
			*iterator->child_id = data.mv_data;
		}

		iterator->child_iterated_once = true;
		break;
	default:
		assert(0);
		ret = EINVAL;
	};

	return ret;
}

static bool
room_is_space(struct cache *cache, MDB_txn *txn, const char *room_id) {
	assert(cache);
	assert(txn);
	assert(room_id);

	bool is_space = false;
	MDB_dbi dbi = 0;

	if ((get_dbi(ROOM_DB_STATE, txn, &dbi, room_id)) == MDB_SUCCESS) {
		char state_key[] = "m.room.create";
		MDB_val value = {0};
		matrix_json_t *json = NULL;
		struct matrix_state_event sevent;

		if ((get_str(txn, dbi, state_key, &value)) == MDB_SUCCESS
			&& (json = matrix_json_parse((char *) value.mv_data, value.mv_size))
			&& (matrix_event_state_parse(&sevent, json)) == 0) {
			if (sevent.type != MATRIX_ROOM_CREATE) {
				fprintf(stderr,
				  "m.room.create state event isn't a state event in room "
				  "'%s'!\n",
				  room_id);
				assert(0);
			} else {
				is_space = !!sevent.create.type
						&& (strcmp(sevent.create.type, "m.space") == 0);
			}
		}

		matrix_json_delete(json);
	}

	return is_space;
}

int
cache_room_info_init(
  struct cache *cache, struct room_info *info, const char *room_id) {
	assert(cache);
	assert(info);
	assert(room_id);

	MDB_txn *txn = NULL;

	int ret = get_txn(cache, MDB_RDONLY, &txn);

	if (ret == MDB_SUCCESS) {
		*info = (struct room_info) {
		  .invite = false, /* TODO */
		  .is_space = room_is_space(cache, txn, room_id),
		  .name = cache_room_name(cache, txn, room_id),
		  .topic = cache_room_topic(cache, txn, room_id),
		};
	}

	mdb_txn_commit(txn);

	return ret;
}

void
cache_room_info_finish(struct room_info *info) {
	if (info) {
		free(info->name);
		free(info->topic);
		memset(info, 0, sizeof(*info));
	}
}

int
cache_save_txn_init(
  struct cache *cache, struct cache_save_txn *txn, const char *room_id) {
	assert(cache);
	assert(cache->env);
	assert(txn);
	assert(room_id);

	*txn = (struct cache_save_txn) {
	  /* Start in the middle so we can easily backfill while filling in
	   * events forward aswell. */
	  .index = UINT64_MAX / 2,
	  .cache = cache,
	  .room_id = room_id};

	return get_txn(cache, 0, &txn->txn);
}

void
cache_save_txn_finish(struct cache_save_txn *txn) {
	if (txn) {
		int ret = mdb_txn_commit(txn->txn);

		if (ret != MDB_SUCCESS) {
			fprintf(stderr, "Failed to commit txn: %s\n", mdb_strerror(ret));
			abort(); /* TODO better handling. */
		}
	}
}

int
cache_set_room_dbs(struct cache_save_txn *txn, struct matrix_room *room) {
	assert(txn);
	assert(room);

	int ret = 0;

	for (enum room_db i = 0; i < ROOM_DB_MAX; i++) {
		if ((ret = get_dbi(i, txn->txn, &txn->dbs[i], room->id))
			!= MDB_SUCCESS) {
			return ret;
		}
	}

	MDB_cursor *cursor = NULL;

	if ((ret = mdb_cursor_open(
		   txn->txn, txn->dbs[ROOM_DB_ORDER_TO_EVENTS], &cursor))
		== MDB_SUCCESS) {
		MDB_val key = {0};
		MDB_val val = {0};

		if ((mdb_cursor_get(cursor, &key, &val, MDB_LAST)) == MDB_SUCCESS) {
			assert(key.mv_size == sizeof(txn->index));
			memcpy(&txn->index, key.mv_data, sizeof(txn->index));

			txn->index++; /* Don't overwrite the last event. */
		}

		mdb_cursor_close(cursor);
	}

	return ret;
}

int
cache_save_room(struct cache_save_txn *txn, struct matrix_room *room) {
	assert(txn);
	assert(room);

	/* TODO dump summary. */
	return put_str(
	  txn->txn, txn->cache->dbs[DB_ROOMS], room->id, (char[]) {""}, 0);
}

static bool
child_event_in_parent_space(
  struct cache *cache, MDB_txn *txn, const char *parent, const char *child) {
	assert(cache);
	assert(txn);
	assert(parent);
	assert(child);

	MDB_cursor *cursor = NULL;

	int ret = mdb_cursor_open(txn, cache->dbs[DB_SPACE_CHILDREN], &cursor);

	if (ret != MDB_SUCCESS) {
		fprintf(stderr,
		  "Failed to open cursor to check child event for '%s' in space '%s': "
		  "%s\n",
		  child, parent, mdb_strerror(ret));
		return false;
	}

	ret
	  = mdb_cursor_get(cursor, &(MDB_val) {strlen(parent) + 1, noconst(parent)},
		&(MDB_val) {strlen(child) + 1, noconst(child)}, MDB_GET_BOTH);

	mdb_cursor_close(cursor);

	if (ret != MDB_SUCCESS) {
		fprintf(stderr,
		  "Child event for '%s' doesn't exist in space '%s': %s\n", child,
		  parent, mdb_strerror(ret));
	}

	return (ret == MDB_SUCCESS);
}

enum cache_save_error
cache_save_event(struct cache_save_txn *txn, struct matrix_sync_event *event) {
	assert(txn);
	assert(event);

	/* TODO power level checking. */

	int ret = MDB_SUCCESS;

	switch (event->type) {
	case MATRIX_EVENT_STATE:
		{
			struct matrix_state_event *sevent = &event->state;

			switch (sevent->type) {
#define ensure_state_key(sevent)                                               \
	do {                                                                       \
		if (!(sevent)->state_key || (strnlen((sevent)->state_key, 1)) == 0) {  \
			assert(0);                                                         \
			return CACHE_FAIL;                                                 \
		}                                                                      \
	} while (0)
			case MATRIX_ROOM_MEMBER:
				ensure_state_key(sevent);

				{
					char *data = matrix_json_print(event->json);
					ret = put_str(txn->txn, txn->dbs[ROOM_DB_MEMBERS],
					  sevent->state_key, data, 0);
					free(data);
				}
				break;
			case MATRIX_ROOM_SPACE_CHILD:
				ensure_state_key(sevent);

				if (!(room_is_space(txn->cache, txn->txn, txn->room_id))) {
					break;
				}

				if (!sevent->space_child.via) {
					del_str(txn->txn, txn->cache->dbs[DB_SPACE_CHILDREN],
					  txn->room_id, sevent->state_key);
					break;
				}

				if ((ret = put_str(txn->txn, txn->cache->dbs[DB_SPACE_CHILDREN],
					   txn->room_id, sevent->state_key, MDB_NODUPDATA))
					== MDB_KEYEXIST) {
					fprintf(stderr,
					  "Tried to add child '%s' already present in space '%s'\n",
					  sevent->state_key, txn->room_id);
				}
				break;
			case MATRIX_ROOM_SPACE_PARENT:
				ensure_state_key(sevent);

				if (!(child_event_in_parent_space(
					  txn->cache, txn->txn, sevent->state_key, txn->room_id))) {
					break;
				}

				/* TODO if (!sender_has_power_level_in_parent) { break; } */

				if (!sevent->space_parent.via) {
					del_str(txn->txn, txn->cache->dbs[DB_SPACE_CHILDREN],
					  sevent->state_key, txn->room_id);
					break;
				}

				if ((ret = put_str(txn->txn, txn->cache->dbs[DB_SPACE_CHILDREN],
					   sevent->state_key, txn->room_id, MDB_NODUPDATA))
					== MDB_KEYEXIST) {
					fprintf(stderr,
					  "Tried to add child '%s' already present in space '%s'\n",
					  txn->room_id, sevent->state_key);
				}
				break;
#undef ensure_state_key
			default:
				if (!sevent->state_key
					|| (strnlen(sevent->state_key, 1)) == 0) {
					char *data = matrix_json_print(event->json);
					ret = put_str(txn->txn, txn->dbs[ROOM_DB_STATE],
					  sevent->base.type, data, 0);
					free(data);
				} else {
					fprintf(stderr,
					  "Ignoring unknown state event with state key '%s'\n",
					  sevent->state_key);
				}
				break;
			}
		}
		break;
	case MATRIX_EVENT_TIMELINE:
		{
			struct matrix_timeline_event *tevent = &event->timeline;

			if (tevent->type == MATRIX_ROOM_REDACTION) {
				MDB_val del_index = {0};
				bool set_index = false;

				if ((get_str(txn->txn, txn->dbs[ROOM_DB_EVENTS_TO_ORDER],
					  tevent->redaction.redacts, &del_index))
					== MDB_SUCCESS) {
					mdb_del(txn->txn, txn->dbs[ROOM_DB_ORDER_TO_EVENTS],
					  &del_index, NULL);

					assert(del_index.mv_size == sizeof(txn->latest_redaction));
					memcpy(&txn->latest_redaction, del_index.mv_data,
					  del_index.mv_size);

					set_index = true;
				}

				del_str(txn->txn, txn->dbs[ROOM_DB_EVENTS_TO_ORDER],
				  tevent->base.event_id, NULL);
				del_str(txn->txn, txn->dbs[ROOM_DB_EVENTS],
				  tevent->base.event_id, NULL);

				if (set_index) {
					return CACHE_GOT_REDACTION;
				}
			} else {
				char *data = matrix_json_print(event->json);

				ret = put_str(txn->txn, txn->dbs[ROOM_DB_EVENTS],
				  tevent->base.event_id, data, MDB_NOOVERWRITE);

				if (ret == MDB_KEYEXIST) {
					fprintf(stderr, "Got duplicate event '%s' in room '%s'\n",
					  tevent->base.event_id, txn->room_id);
					free(data);

					return CACHE_FAIL;
				}

				if (ret == MDB_SUCCESS
					&& (ret
						 = put_int(txn->txn, txn->dbs[ROOM_DB_ORDER_TO_EVENTS],
						   txn->index, tevent->base.event_id, 0))
						 == MDB_SUCCESS
					&& (ret = put_str_int(txn->txn,
						  txn->dbs[ROOM_DB_EVENTS_TO_ORDER],
						  tevent->base.event_id, txn->index, 0))
						 == MDB_SUCCESS) {
					txn->index++;
				}

				free(data);
			}
		}
		break;
	default:
		break;
	}

	if (ret != MDB_SUCCESS) {
		fprintf(stderr, "Failed to save event '%s' in room '%s': %s\n",
		  matrix_sync_event_id(event), txn->room_id, mdb_strerror(ret));
	}

	return CACHE_SUCCESS;
}

char *
noconst(const char *str) {
	return (char *) str;
}
