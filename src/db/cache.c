/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* Uses Nheko's cache as reference:
 * https://github.com/Nheko-Reborn/nheko/blob/master/src/Cache.cpp */
#include "db/cache.h"

#include "stb_ds.h"
#include "util/log.h"

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

static const unsigned db_flags[DB_MAX] = {
  [DB_SPACE_CHILDREN] = MDB_DUPSORT,
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
  [ROOM_DB_RELATIONS] = "relations",
  [ROOM_DB_MEMBERS] = "members",
  [ROOM_DB_STATE] = "state",
  [ROOM_DB_SPACE_PARENT] = "space_parent",
  [ROOM_DB_SPACE_CHILD] = "space_child",
};

static const unsigned room_db_flags[ROOM_DB_MAX] = {
  [ROOM_DB_ORDER_TO_EVENTS] = MDB_INTEGERKEY,
  [ROOM_DB_RELATIONS] = MDB_DUPSORT,
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

/* Convert MDB_val to uint64_t with size verification. */
static void
cpy_index(MDB_val *index, uint64_t *out_index) {
	assert(index);
	assert(out_index);

	if (index->mv_size != sizeof(*out_index)) {
		LOG(LOG_ERROR,
		  "Expected size %zu for index but got %zu! Corrupt database?",
		  sizeof(*out_index), index->mv_size);
		abort();
	}

	memcpy(out_index, index->mv_data, sizeof(*out_index));
}

/* Must be a macro for getting line numbers for LOG(). */
#define ABORT_OR_RETURN(result)                                                \
	do {                                                                       \
		int _res = (result);                                                   \
		switch (_res) {                                                        \
		case MDB_SUCCESS:                                                      \
		case MDB_KEYEXIST:                                                     \
		case MDB_NOTFOUND:                                                     \
			break;                                                             \
		default:                                                               \
			LOG(LOG_ERROR, "LMDB returned failure: %s", mdb_strerror(_res));   \
			abort();                                                           \
		}                                                                      \
		return _res;                                                           \
	} while (0)

static int
commit(MDB_txn *txn) {
	ABORT_OR_RETURN(mdb_txn_commit(txn));
}

#define mdb_txn_commit(txn) commit(txn)

static int
get_dbi(enum room_db db, MDB_txn *txn, MDB_dbi *dbi, const char *room_id) {
	assert(txn);
	assert(dbi);
	assert(room_id);
	assert(db >= 0 && db < ROOM_DB_MAX);

	char *room = NULL;

	if ((asprintf(&room, "%s/%s", room_id, room_db_names[db])) == -1) {
		return ENOMEM;
	}

	int ret = mdb_dbi_open(txn, room, MDB_CREATE | room_db_flags[db], dbi);
	free(room);

	ABORT_OR_RETURN(ret);
}

static int
del_str(MDB_txn *txn, MDB_dbi dbi, const char *key, const char *data) {
	if (!key) {
		return EINVAL;
	}

	ABORT_OR_RETURN(
	  mdb_del(txn, dbi, &(MDB_val) {strlen(key) + 1, noconst(key)},
		(data ? &(MDB_val) {strlen(data) + 1, noconst(data)} : NULL)));
}

static int
get_str(MDB_txn *txn, MDB_dbi dbi, const char *key, MDB_val *data) {
	if (!txn || !key || !data) {
		return EINVAL;
	}

	ABORT_OR_RETURN(
	  mdb_get(txn, dbi, &(MDB_val) {strlen(key) + 1, noconst(key)}, data));
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

	ABORT_OR_RETURN(
	  mdb_put(txn, dbi, &(MDB_val) {strlen(key) + 1, noconst(key)},
		&(MDB_val) {strlen(data) + 1, noconst(data)}, flags));
}

static int
put_int(
  MDB_txn *txn, MDB_dbi dbi, uint64_t key, const char *data, unsigned flags) {
	if (!txn || !data) {
		return EINVAL;
	}

	ABORT_OR_RETURN(mdb_put(txn, dbi, &(MDB_val) {sizeof(key), &key},
	  &(MDB_val) {strlen(data) + 1, noconst(data)}, flags));
}

static int
put_str_int(
  MDB_txn *txn, MDB_dbi dbi, const char *key, uint64_t data, unsigned flags) {
	if (!txn || !key) {
		return EINVAL;
	}

	ABORT_OR_RETURN(
	  mdb_put(txn, dbi, &(MDB_val) {strlen(key) + 1, noconst(key)},
		&(MDB_val) {sizeof(data), &data}, flags));
}

static int
get_txn(struct cache *cache, unsigned flags, MDB_txn **txn) {
	assert(cache);

	ABORT_OR_RETURN(mdb_txn_begin(cache->env, NULL, flags, txn));
}

static bool
is_str(MDB_val *val) {
	assert(val);

	return (
	  val->mv_size > 0 && ((char *) val->mv_data)[val->mv_size - 1] == '\0');
}

static int
cache_rooms_next(struct cache_iterator *iterator) {
	assert(iterator);
	assert(iterator->type == CACHE_ITERATOR_ROOMS);

	MDB_val key = {0};
	MDB_val data = {0};

	int ret = mdb_cursor_get(iterator->cursor, &key, &data, MDB_NEXT);

	if (ret != MDB_SUCCESS) {
		return ret;
	}

	assert(is_str(&key));
	assert(is_str(&data));

	*iterator->room_id = key.mv_data;
	return ret;
}

static int
cache_event_next(struct cache_iterator *iterator) {
	assert(iterator);
	assert(iterator->type == CACHE_ITERATOR_EVENTS);

	matrix_json_delete(iterator->event_json);
	iterator->event_json = NULL;

	for (;;) {
		if (iterator->num_fetch == 0) {
			return EINVAL;
		}

		MDB_val db_index = {0};
		MDB_val id = {0};

		int ret = (iterator->fetched_once ? (
					 mdb_cursor_get(iterator->cursor, &db_index, &id, MDB_PREV))
										  : mdb_cursor_get(iterator->cursor,
											&db_index, &id, MDB_GET_CURRENT));

		iterator->fetched_once = true;

		if (ret != MDB_SUCCESS) {
			return ret;
		}

		assert(is_str(&id));

		MDB_val db_json = {0};
		ret = mdb_get(iterator->txn, iterator->events_dbi, &id, &db_json);

		if (ret != MDB_SUCCESS) {
			return ret;
		}

		assert(is_str(&db_json));

		uint64_t index = 0;
		cpy_index(&db_index, &index);

		matrix_json_t *json
		  = matrix_json_parse(db_json.mv_data, db_json.mv_size);
		assert(json);

		ret = matrix_event_sync_parse(&iterator->event->event, json);

		if (ret != 0) {
			if (iterator->event->event.type == MATRIX_EVENT_TIMELINE
				&& !(matrix_json_has_content(json))) {
				/* A redacted event. */
				matrix_json_delete(json);
				continue;
			}

			LOG(LOG_ERROR, "Failed to parse event JSON '%s'",
			  (char *) db_json.mv_data);
			abort();
		}

		switch (iterator->event->event.type) {
		case MATRIX_EVENT_STATE:
			if ((iterator->state_events & iterator->event->event.state.type)
				!= iterator->event->event.state.type) {
				matrix_json_delete(json);
				continue;
			}

			iterator->event->event.state.is_in_timeline = true;
			break;
		case MATRIX_EVENT_TIMELINE:
			if ((iterator->timeline_events
				  & iterator->event->event.timeline.type)
				!= iterator->event->event.timeline.type) {
				matrix_json_delete(json);
				continue;
			}
			break;
		default:
			assert(0);
		}

		iterator->num_fetch--;

		iterator->event->index = index;
		iterator->event_json = json;

		return ret;
	}
}

static int
cache_member_next(struct cache_iterator *iterator) {
	assert(iterator);
	assert(iterator->type == CACHE_ITERATOR_MEMBER);

	MDB_val key = {0};
	MDB_val data = {0};

	matrix_json_delete(iterator->member_json);
	iterator->member_json = NULL;

	int ret = mdb_cursor_get(iterator->cursor, &key, &data, MDB_NEXT);

	if (ret != MDB_SUCCESS) {
		return ret;
	}

	assert(is_str(&key));
	assert(is_str(&data));

	matrix_json_t *json = matrix_json_parse(data.mv_data, data.mv_size);
	assert(json);

	struct matrix_state_event event = {0};

	ret = matrix_event_state_parse(&event, json);

	if (ret != 0 || event.type != MATRIX_ROOM_MEMBER) {
		LOG(
		  LOG_ERROR, "Failed to parse member JSON '%s'", (char *) data.mv_data);
		assert(0);

		matrix_json_delete(json);
		return EINVAL;
	}

	*iterator->member = (struct cache_iterator_member) {
	  .mxid = key.mv_data,
	  .username = event.content.member.displayname,
	};

	iterator->member_json = json;

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

static int
cache_spaces_next(struct cache_iterator *iterator) {
	assert(iterator);
	assert(iterator->type == CACHE_ITERATOR_SPACES);

	MDB_val key = {0};
	MDB_val data = {0};

	int ret = mdb_cursor_get(iterator->cursor, &key, &data, MDB_NEXT);

	if (ret != MDB_SUCCESS) {
		return ret;
	}

	assert(is_str(&key));

	iterator->space->id = key.mv_data;
	return cache_iterator_space_children(iterator->cache, iterator->cursor,
	  &iterator->space->children_iterator, &iterator->space->child_id);
}

static int
cache_space_children_next(struct cache_iterator *iterator) {
	assert(iterator);
	assert(iterator->type == CACHE_ITERATOR_SPACE_CHILDREN);

	MDB_val key = {0};
	MDB_val data = {0};

	int ret = mdb_cursor_get(iterator->cursor, &key, &data,
	  (iterator->child_iterated_once ? MDB_NEXT_DUP : MDB_FIRST_DUP));

	if (ret != MDB_SUCCESS) {
		return ret;
	}

	iterator->child_iterated_once = true;

	assert(is_str(&data));
	*iterator->child_id = data.mv_data;

	return ret;
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
  uint64_t num_fetch, unsigned timeline_events, unsigned state_events) {
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
	  .timeline_events = timeline_events,
	  .state_events = state_events,
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
		mdb_txn_commit(txn);
		return ret;
	}

	*iterator = (struct cache_iterator) {.type = CACHE_ITERATOR_SPACES,
	  .txn = txn,
	  .cursor = cursor,
	  .cache = cache,
	  .space = space};

	return ret;
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
			if ((ret = mdb_dbi_open(
				   txn, db_names[i], MDB_CREATE | db_flags[i], &cache->dbs[i]))
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
					res = sevent.content.name.name;
					break;
				case MATRIX_ROOM_CANONICAL_ALIAS:
					res = sevent.content.canonical_alias.alias;
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
				res = sevent.content.topic.topic;
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

static int (*const iterators[CACHE_ITERATOR_MAX])(struct cache_iterator *)
  = {[CACHE_ITERATOR_ROOMS] = cache_rooms_next,
	[CACHE_ITERATOR_EVENTS] = cache_event_next,
	[CACHE_ITERATOR_MEMBER] = cache_member_next,
	[CACHE_ITERATOR_SPACES] = cache_spaces_next,
	[CACHE_ITERATOR_SPACE_CHILDREN] = cache_space_children_next};

int
cache_iterator_next(struct cache_iterator *iterator) {
	assert(iterator);

	if (iterator->type >= 0 && iterator->type < CACHE_ITERATOR_MAX) {
		return iterators[iterator->type](iterator);
	}

	assert(0);
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
				LOG(LOG_ERROR,
				  "m.room.create state event isn't a state event in room "
				  "'%s'!",
				  room_id);
				assert(0);
			} else {
				is_space = !!sevent.content.create.type
						&& (strcmp(sevent.content.create.type, "m.space") == 0);
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
		mdb_txn_commit(txn->txn);
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
			cpy_index(&key, &txn->index);
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

static int
save_json_with_index(struct cache_save_txn *txn,
  struct matrix_sync_event *event, uint64_t *index) {
	assert(txn);
	assert(event);
	assert(index);

	const char *event_id = matrix_sync_event_id(event);
	assert(event_id);

	char *data = matrix_json_print(event->json);
	assert(data);

	/* TODO sort by index */
	if (event->type == MATRIX_EVENT_TIMELINE
		&& event->timeline.relation.event_id) {
		put_str(txn->txn, txn->dbs[ROOM_DB_RELATIONS], event_id,
		  event->timeline.relation.event_id, 0);
	}

	int ret = put_str(
	  txn->txn, txn->dbs[ROOM_DB_EVENTS], event_id, data, MDB_NOOVERWRITE);

	if (ret == MDB_SUCCESS
		&& (ret = put_int(txn->txn, txn->dbs[ROOM_DB_ORDER_TO_EVENTS],
			  txn->index, event_id, 0))
			 == MDB_SUCCESS
		&& (ret = put_str_int(txn->txn, txn->dbs[ROOM_DB_EVENTS_TO_ORDER],
			  event_id, txn->index, 0))
			 == MDB_SUCCESS) {
		*index = txn->index;
		txn->index++;
	}

	free(data);

	return ret;
}

/* TODO Just clean up and re-create the relations of all rooms with any
 * space-related change. */
enum cache_deferred_ret
cache_process_deferred_event(
  struct cache *cache, struct cache_deferred_space_event *deferred_event) {
	assert(cache);
	assert(deferred_event);

	MDB_txn *txn = NULL;
	int mdb_ret = get_txn(cache, 0, &txn);

	assert(mdb_ret == MDB_SUCCESS);

	enum cache_deferred_ret ret = CACHE_DEFERRED_FAIL;

	struct matrix_state_event sevent = {0};

	switch (deferred_event->type) {
	case MATRIX_ROOM_SPACE_CHILD:
		{
			if (!(room_is_space(cache, txn, deferred_event->parent))) {
				LOG(LOG_WARN, "Tried to add child '%s' to non-space room '%s'",
				  deferred_event->child, deferred_event->parent);
				break;
			}

			/* Neither a m.space.child event, nor a m.space.parent event
			 * should be present for the relation to be broken. */
			if (deferred_event->via_was_null) {
				MDB_dbi child_parent_db = {0};
				mdb_ret = get_dbi(ROOM_DB_SPACE_PARENT, txn, &child_parent_db,
				  deferred_event->child);
				assert(mdb_ret == MDB_SUCCESS);
				bool parent_event_in_child = false;

				MDB_val data = {0};
				mdb_ret = get_str(
				  txn, child_parent_db, deferred_event->parent, &data);

				if (mdb_ret == MDB_SUCCESS) {
					assert(is_str(&data));
					matrix_json_t *json
					  = matrix_json_parse(data.mv_data, data.mv_size);
					assert(json);
					int matrix_ret = matrix_event_state_parse(&sevent, json);
					assert(matrix_ret == 0);
					assert(sevent.type == MATRIX_ROOM_SPACE_PARENT);
					parent_event_in_child = !!sevent.content.space_parent.via;
					matrix_json_delete(json);
				}

				if (!parent_event_in_child) {
					del_str(txn, cache->dbs[DB_SPACE_CHILDREN],
					  deferred_event->parent, deferred_event->child);
					LOG(LOG_MESSAGE, "Removed child '%s' from space '%s'",
					  deferred_event->child, deferred_event->parent);
					ret = CACHE_DEFERRED_REMOVED;
				}
				break;
			}

			if ((mdb_ret = put_str(txn, cache->dbs[DB_SPACE_CHILDREN],
				   deferred_event->parent, deferred_event->child,
				   MDB_NODUPDATA))
				== MDB_KEYEXIST) {
				LOG(LOG_WARN,
				  "Tried to add child '%s' already present in space '%s'",
				  deferred_event->child, deferred_event->parent);
			} else {
				assert(mdb_ret == MDB_SUCCESS);
				LOG(LOG_MESSAGE, "Added child '%s' to space '%s'",
				  deferred_event->child, deferred_event->parent);
				ret = CACHE_DEFERRED_ADDED;
			}
		}
		break;
	case MATRIX_ROOM_SPACE_PARENT:
		{
			MDB_dbi parent_child_db = {0};
			MDB_dbi parent_state_db = {0};

			mdb_ret = get_dbi(ROOM_DB_SPACE_CHILD, txn, &parent_child_db,
			  deferred_event->parent);
			assert(mdb_ret == MDB_SUCCESS);

			mdb_ret = get_dbi(
			  ROOM_DB_STATE, txn, &parent_state_db, deferred_event->parent);
			assert(mdb_ret == MDB_SUCCESS);

			bool child_event_in_parent = false;

			MDB_val data = {0};
			mdb_ret
			  = get_str(txn, parent_child_db, deferred_event->child, &data);

			if (mdb_ret == MDB_SUCCESS) {
				assert(is_str(&data));
				matrix_json_t *json
				  = matrix_json_parse(data.mv_data, data.mv_size);
				assert(json);
				int matrix_ret = matrix_event_state_parse(&sevent, json);
				assert(matrix_ret == 0);
				assert(sevent.type == MATRIX_ROOM_SPACE_CHILD);
				child_event_in_parent = !!sevent.content.space_child.via;
				matrix_json_delete(json);
			}

			bool sender_has_power_level_in_parent = false; /* TODO */

			/* Only 1 condition needs to be true for the event to be
			 * valid. */
			if (!child_event_in_parent && !sender_has_power_level_in_parent) {
				LOG(LOG_WARN,
				  "Child event not present in parent space and sender "
				  "doesn't have enough powers to add room '%s' to "
				  "space '%s'",
				  deferred_event->child, deferred_event->parent);
				break;
			}

			/* Neither a m.space.child event, nor a m.space.parent event
			 * should be present for the relation to be broken. */
			if (deferred_event->via_was_null) {
				if (!child_event_in_parent) {
					del_str(txn, cache->dbs[DB_SPACE_CHILDREN],
					  deferred_event->parent, deferred_event->child);
					LOG(LOG_MESSAGE, "Removed child '%s' from space '%s'",
					  deferred_event->child, deferred_event->parent);
					ret = CACHE_DEFERRED_REMOVED;
				}
				break;
			}

			if ((mdb_ret = put_str(txn, cache->dbs[DB_SPACE_CHILDREN],
				   deferred_event->parent, deferred_event->child,
				   MDB_NODUPDATA))
				== MDB_KEYEXIST) {
				LOG(LOG_WARN,
				  "Tried to add child '%s' already present in space "
				  "'%s'",
				  deferred_event->child, deferred_event->parent);
			} else {
				assert(mdb_ret == MDB_SUCCESS);
				LOG(LOG_MESSAGE, "Added child '%s' to space '%s'",
				  deferred_event->child, deferred_event->parent);
				ret = CACHE_DEFERRED_ADDED;
			}
		}
		break;
	default:
		assert(0);
	}

	mdb_txn_commit(txn);
	return ret;
}

enum cache_save_error
cache_save_event(struct cache_save_txn *txn, struct matrix_sync_event *event,
  uint64_t *index, uint64_t *redaction_index,
  struct cache_deferred_space_event **deferred_events) {
	assert(txn);
	assert(event);
	assert(index);
	assert(redaction_index);
	assert(deferred_events);

	*index = (uint64_t) -1;
	*redaction_index = (uint64_t) -1;

	/* TODO power level checking. */

	switch (event->type) {
	case MATRIX_EVENT_STATE:
		{
			struct matrix_state_event *sevent = &event->state;
			assert(sevent->base.state_key);

			if (sevent->is_in_timeline
				&& (save_json_with_index(txn, event, index)) == MDB_KEYEXIST) {
				return CACHE_EVENT_IGNORED;
			}

			switch (sevent->type) {
			case MATRIX_ROOM_MEMBER:
				{
					char *data = matrix_json_print(event->json);
					put_str(txn->txn, txn->dbs[ROOM_DB_MEMBERS],
					  sevent->base.state_key, data, 0);
					free(data);
				}

				return CACHE_EVENT_SAVED;
			case MATRIX_ROOM_SPACE_CHILD:
				assert((strnlen(sevent->base.state_key, 1)) > 0);

				{
					char *data = matrix_json_print(event->json);
					put_str(txn->txn, txn->dbs[ROOM_DB_SPACE_CHILD],
					  sevent->base.state_key, data, 0);
					free(data);
					arrput(*deferred_events,
					  ((struct cache_deferred_space_event) {
						.via_was_null = !sevent->content.space_child.via,
						.type = sevent->type,
						.parent = txn->room_id,
						.child = sevent->base.state_key,
						.sender = sevent->base.sender}));
				}

				return CACHE_EVENT_DEFERRED;
			case MATRIX_ROOM_SPACE_PARENT:
				assert((strnlen(sevent->base.state_key, 1)) > 0);

				{
					char *data = matrix_json_print(event->json);
					put_str(txn->txn, txn->dbs[ROOM_DB_SPACE_PARENT],
					  sevent->base.state_key, data, 0);
					free(data);
					arrput(*deferred_events,
					  ((struct cache_deferred_space_event) {
						.via_was_null = !sevent->content.space_parent.via,
						.type = sevent->type,
						.parent = sevent->base.state_key,
						.child = txn->room_id,
						.sender = sevent->base.sender}));
				}

				return CACHE_EVENT_DEFERRED;
			default:
				/* Empty state key */
				if ((strnlen(sevent->base.state_key, 1)) == 0) {
					char *data = matrix_json_print(event->json);
					put_str(txn->txn, txn->dbs[ROOM_DB_STATE],
					  sevent->base.type, data, 0);
					free(data);

					return CACHE_EVENT_SAVED;
				} else {
					LOG(LOG_WARN,
					  "Ignoring unknown state event with state key '%s'",
					  sevent->base.state_key);

					return CACHE_EVENT_IGNORED;
				}
			}
		}
	case MATRIX_EVENT_TIMELINE:
		{
			struct matrix_timeline_event *tevent = &event->timeline;

			if ((save_json_with_index(txn, event, index)) == MDB_KEYEXIST) {
				return CACHE_EVENT_IGNORED;
			}

			if (tevent->type == MATRIX_ROOM_REDACTION) {
				MDB_val del_index = {0};

				if ((get_str(txn->txn, txn->dbs[ROOM_DB_EVENTS_TO_ORDER],
					  tevent->redaction.redacts, &del_index))
					== MDB_SUCCESS) {
					MDB_val raw_json = {0};

					int ret = get_str(txn->txn, txn->dbs[ROOM_DB_EVENTS],
					  tevent->redaction.redacts, &raw_json);

					assert(ret == MDB_SUCCESS);
					assert(is_str(&raw_json));

					matrix_json_t *json
					  = matrix_json_parse(raw_json.mv_data, raw_json.mv_size);
					assert(json);

					matrix_json_clear_content(json);

					char *cleaned_json = matrix_json_print(json);
					assert(cleaned_json);

					ret = put_str(txn->txn, txn->dbs[ROOM_DB_EVENTS],
					  tevent->redaction.redacts, cleaned_json, 0);

					assert(ret == MDB_SUCCESS);

					free(cleaned_json);
					matrix_json_delete(json);

					cpy_index(&del_index, redaction_index);
				} else {
					LOG(LOG_WARN,
					  "Got redaction '%s' for unknown event '%s' in room "
					  "'%s'",
					  tevent->base.event_id, tevent->redaction.redacts,
					  txn->room_id);

					return CACHE_EVENT_IGNORED;
				}
			}
			return CACHE_EVENT_SAVED;
		default:
			return CACHE_EVENT_IGNORED;
		}
	}
}

char *
noconst(const char *str) {
	/* NOLINTNEXTLINE(clang-diagnostic-cast-qual) */
	return (char *) str;
}
