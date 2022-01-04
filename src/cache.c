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

#define STREQ(s1, s2) ((strcmp(s1, s2)) == 0)

static const char *const db_names[DB_MAX] = {
  [DB_AUTH] = "auth",
  [DB_ROOMS] = "rooms",
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

/* Hack to pass const char arrays to MDB APIs that don't modify them but don't
 * explicitly mark their arguments as const either. */
static char *
noconst(const char *str) {
	return (char *) str;
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
del_str(MDB_txn *txn, MDB_dbi dbi, char *key) {
	if (!key) {
		return EINVAL;
	}

	return mdb_del(txn, dbi, &(MDB_val) {strlen(key) + 1, key}, NULL);
}

static int
get_str(MDB_txn *txn, MDB_dbi dbi, char *key, MDB_val *data) {
	if (!txn || !key || !data) {
		return EINVAL;
	}

	return mdb_get(txn, dbi, &(MDB_val) {strlen(key) + 1, key}, data);
}

static char *
get_str_and_dup(MDB_txn *txn, MDB_dbi dbi, char *key) {
	if (txn && key) {
		MDB_val data = {0};

		if ((get_str(txn, dbi, key, &data)) == MDB_SUCCESS) {
			return strndup(data.mv_data, data.mv_size);
		}
	}

	return NULL;
}

static int
put_str(MDB_txn *txn, MDB_dbi dbi, char *key, char *data, unsigned flags) {
	if (!txn || !key || !data) {
		return EINVAL;
	}

	return mdb_put(txn, dbi, &(MDB_val) {strlen(key) + 1, key},
	  &(MDB_val) {strlen(data) + 1, data}, flags);
}

static int
put_int(MDB_txn *txn, MDB_dbi dbi, uint64_t key, char *data, unsigned flags) {
	if (!txn || !data) {
		return EINVAL;
	}

	return mdb_put(txn, dbi, &(MDB_val) {sizeof(key), &key},
	  &(MDB_val) {strlen(data) + 1, data}, flags);
}

static int
put_str_int(
  MDB_txn *txn, MDB_dbi dbi, char *key, uint64_t data, unsigned flags) {
	if (!txn || !key) {
		return EINVAL;
	}

	return mdb_put(txn, dbi, &(MDB_val) {strlen(key) + 1, key},
	  &(MDB_val) {sizeof(data), &data}, flags);
}

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

	return ((*iterator->room_id) = strndup(key->mv_data, key->mv_size))
		   ? MDB_SUCCESS
		   : ENOMEM;
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
	struct matrix_state_event event = {0};

	int ret = matrix_event_state_parse(&event, json);

	if (ret != 0) {
		return ret;
	}

	assert(event.type == MATRIX_ROOM_MEMBER);

	*iterator->member = (struct cache_iterator_member) {
	  .mxid = key->mv_data,
	  .username = event.member.displayname,
	  .json = json,
	};

	return 0;
}

int
cache_iterator_rooms(
  struct cache *cache, struct cache_iterator *iterator, char **room_id) {
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

void
cache_iterator_finish(struct cache_iterator *iterator) {
	if (iterator) {
		mdb_cursor_close(iterator->cursor);
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
	};

	char dir[] = "/tmp/db";

	int ret = 0;

	if ((ret = mkdir_parents(dir, dir_perms)) != 0) {
		return ret;
	}

	MDB_txn *txn = NULL;

	unsigned multiple_readonly_txn_per_thread = MDB_NOTLS;

	if ((ret = mdb_env_create(&cache->env)) == MDB_SUCCESS
		&& (ret = mdb_env_set_maxdbs(cache->env, max_dbs)) == MDB_SUCCESS
		&& (ret = mdb_env_open(
			  cache->env, dir, multiple_readonly_txn_per_thread, db_perms))
			 == MDB_SUCCESS
		&& (ret = get_txn(cache, 0, &txn)) == MDB_SUCCESS) {

		for (size_t i = 0; i < DB_MAX; i++) {
			if ((ret
				  = mdb_dbi_open(txn, db_names[i], MDB_CREATE, &cache->dbs[i]))
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
	default:
		assert(0);
		ret = EINVAL;
	};

	return ret;
}

struct room_info *
cache_room_info(struct cache *cache, const char *room_id) {
	assert(cache);
	assert(room_id);

	struct room_info *info = malloc(sizeof(*info));

	if (info) {
		MDB_txn *txn = NULL;

		if ((get_txn(cache, MDB_RDONLY, &txn)) == MDB_SUCCESS) {
			*info = (struct room_info) {
			  .invite = false,
			  .space = false,
			  .name = cache_room_name(cache, txn, room_id),
			  .topic = cache_room_topic(cache, txn, room_id),
			};

			mdb_txn_commit(txn);

			return info;
		}

		free(info);
	}

	return NULL;
}

void
room_info_destroy(struct room_info *info) {
	if (info) {
		free(info->name);
		free(info->topic);
		free(info);
	}
}

int
cache_save_txn_init(struct cache *cache, struct cache_save_txn *txn) {
	if (!cache || !cache->env || !txn) {
		return -1;
	}

	*txn = (struct cache_save_txn) {
	  /* Start in the middle so we can easily backfill while filling in
	   * events forward aswell. */
	  .index = UINT64_MAX / 2,
	  .cache = cache};

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

	if (room->type == MATRIX_ROOM_LEAVE) {
		for (enum room_db i = 0; i < ROOM_DB_MAX; i++) {
			mdb_drop(txn->txn, txn->dbs[i], true);
		}

		return EINVAL; /* TODO better return code. */
	}

	MDB_cursor *cursor = NULL;

	if ((ret = mdb_cursor_open(
		   txn->txn, txn->dbs[ROOM_DB_ORDER_TO_EVENTS], &cursor))
		== MDB_SUCCESS) {
		MDB_val key = {0};
		MDB_val val = {0};

		if ((ret = mdb_cursor_get(cursor, &key, &val, MDB_LAST))
			== MDB_SUCCESS) {
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

enum cache_save_error
cache_save_event(struct cache_save_txn *txn, struct matrix_sync_event *event) {
	assert(txn);
	assert(event);

	switch (event->type) {
	case MATRIX_EVENT_STATE:
		{
			struct matrix_state_event *sevent = &event->state;

			if (sevent->type == MATRIX_ROOM_MEMBER) {
				assert(sevent->state_key);

				if (!sevent->state_key) {
					return CACHE_FAIL;
				}

				if ((STREQ(sevent->member.membership, "leave"))) {
					del_str(
					  txn->txn, txn->dbs[ROOM_DB_MEMBERS], sevent->state_key);
				} else {
					char *data = matrix_json_print(event->json);
					put_str(txn->txn, txn->dbs[ROOM_DB_MEMBERS],
					  sevent->state_key, data, 0);
					free(data);
				}
			} else if (!sevent->state_key
					   || (strnlen(sevent->state_key, 1)) == 0) {
				char *data = matrix_json_print(event->json);
				put_str(txn->txn, txn->dbs[ROOM_DB_STATE], sevent->base.type,
				  data, 0);
				free(data);
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
				  tevent->base.event_id);
				del_str(
				  txn->txn, txn->dbs[ROOM_DB_EVENTS], tevent->base.event_id);

				if (set_index) {
					return CACHE_GOT_REDACTION;
				}
			} else {
				MDB_val value = {0};

				if ((get_str(txn->txn, txn->dbs[ROOM_DB_EVENTS],
					  tevent->base.event_id, &value))
					== MDB_SUCCESS) {
					fprintf(stderr, "Got duplicate event '%s'\n",
					  tevent->base.event_id);
					(void) value;

					return CACHE_FAIL;
				}

				char *data = matrix_json_print(event->json);

				put_int(txn->txn, txn->dbs[ROOM_DB_ORDER_TO_EVENTS], txn->index,
				  tevent->base.event_id, 0);
				put_str_int(txn->txn, txn->dbs[ROOM_DB_EVENTS_TO_ORDER],
				  tevent->base.event_id, txn->index, 0);
				put_str(txn->txn, txn->dbs[ROOM_DB_EVENTS],
				  tevent->base.event_id, data, 0);
				txn->index++;

				free(data);
			}
		}
		break;
	default:
		break;
	}

	return CACHE_SUCCESS;
}
