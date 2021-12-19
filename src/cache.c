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
				return -1;
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

	if (!txn || !dbi || !room_id) {
		return -1;
	}

	unsigned flags = MDB_CREATE;

	if (db == ROOM_DB_ORDER_TO_EVENTS) {
		flags |= MDB_INTEGERKEY;
	}

	char *room = NULL;

	if ((asprintf(&room, "%s/%s", room_id, room_db_names[db])) == -1) {
		return -1;
	}

	int ret = mdb_dbi_open(txn, room, flags, dbi);

	free(room);

	return ret == MDB_SUCCESS ? 0 : -1;
}

static int
del_str(MDB_txn *txn, MDB_dbi dbi, char *key) {
	if (!key) {
		return -1;
	}

	return (mdb_del(txn, dbi, &(MDB_val) {strlen(key) + 1, key}, NULL)
			 == MDB_SUCCESS)
		   ? 0
		   : -1;
}

static int
get_str(MDB_txn *txn, MDB_dbi dbi, char *key, MDB_val *data) {
	if (!txn || !key || !data) {
		return -1;
	}

	return (mdb_get(txn, dbi, &(MDB_val) {strlen(key) + 1, key}, data)
			 == MDB_SUCCESS)
		   ? 0
		   : -1;
}

static char *
get_str_and_dup(MDB_txn *txn, MDB_dbi dbi, char *key) {
	if (txn && key) {
		MDB_val data = {0};

		if ((get_str(txn, dbi, key, &data)) == 0) {
			return strndup(data.mv_data, data.mv_size);
		}
	}

	return NULL;
}

static int
put_str(MDB_txn *txn, MDB_dbi dbi, char *key, char *data, unsigned flags) {
	if (!txn || !key || !data) {
		return -1;
	}

	return (mdb_put(txn, dbi, &(MDB_val) {strlen(key) + 1, key},
			  &(MDB_val) {strlen(data) + 1, data}, flags)
			 == MDB_SUCCESS)
		   ? 0
		   : -1;
}

static int
put_int(MDB_txn *txn, MDB_dbi dbi, uint64_t key, char *data, unsigned flags) {
	if (!txn || !data) {
		return -1;
	}

	return (mdb_put(txn, dbi, &(MDB_val) {sizeof(key), &key},
			  &(MDB_val) {strlen(data) + 1, data}, flags)
			 == MDB_SUCCESS)
		   ? 0
		   : -1;
}

static int
put_str_int(
  MDB_txn *txn, MDB_dbi dbi, char *key, uint64_t data, unsigned flags) {
	if (!txn || !key) {
		return -1;
	}

	return (mdb_put(txn, dbi, &(MDB_val) {strlen(key) + 1, key},
			  &(MDB_val) {sizeof(data), &data}, flags)
			 == MDB_SUCCESS)
		   ? 0
		   : -1;
}

static MDB_txn *
get_txn(struct cache *cache, unsigned flags) {
	MDB_txn *txn = NULL;

	if (cache
		&& (mdb_txn_begin(cache->env, NULL, flags, &txn)) != MDB_SUCCESS) {
		return NULL;
	}

	return txn;
}

static int
cache_iterator_init(struct cache *cache, struct cache_iterator *iterator,
  MDB_dbi dbi, unsigned txn_flags,
  int (*iter_cb)(struct cache_iterator *iterator, MDB_val *key, MDB_val *data),
  void *data) {
	assert(iterator);

	*iterator = (struct cache_iterator) {
	  .txn = get_txn(cache, txn_flags),
	  .iter_cb = iter_cb,
	  .data = data,
	};

	if (iterator->txn
		&& (mdb_cursor_open(iterator->txn, dbi, &iterator->cursor))
			 == MDB_SUCCESS) {
		return 0;
	}

	cache_iterator_finish(iterator);
	return -1;
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
	if (!cache) {
		return -1;
	}

	*cache = (struct cache) {0};

	const mode_t dir_perms = 0755;
	const mode_t db_perms = 0600;

	enum {
		max_dbs = 4096,
		db_size = 1 * 1024 * 1024 * 1024, /* 1 GB */
	};

	char dir[] = "/tmp/db";

	if ((mkdir_parents(dir, dir_perms)) == -1) {
		return -1;
	}

	MDB_txn *txn = NULL;

	unsigned multiple_readonly_txn_per_thread = MDB_NOTLS;

	if ((mdb_env_create(&cache->env)) == MDB_SUCCESS
		&& (mdb_env_set_maxdbs(cache->env, max_dbs)) == MDB_SUCCESS
		&& (mdb_env_open(
			 cache->env, dir, multiple_readonly_txn_per_thread, db_perms))
			 == MDB_SUCCESS
		&& (txn = get_txn(cache, 0))) {

		for (size_t i = 0; i < DB_MAX; i++) {
			if ((mdb_dbi_open(txn, db_names[i], MDB_CREATE, &cache->dbs[i]))
				!= MDB_SUCCESS) {
				cache_finish(cache);
				mdb_txn_commit(txn);
				return -1;
			}
		}

		mdb_txn_commit(txn);
		return 0;
	}

	cache_finish(cache);
	mdb_txn_commit(txn);
	return -1;
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

	MDB_txn *txn = get_txn(cache, 0);

	char *ret = NULL;

	if (txn) {
		ret = get_str_and_dup(txn, cache->dbs[DB_AUTH], noconst(db_keys[key]));
	}

	mdb_txn_commit(txn);

	return ret;
}

int
cache_auth_set(struct cache *cache, enum auth_key key, char *auth) {
	assert(cache);
	assert(auth);

	MDB_txn *txn = get_txn(cache, 0);

	int ret = -1;

	if (txn) {
		ret
		  = ((put_str(txn, cache->dbs[DB_AUTH], noconst(db_keys[key]), auth, 0))
			  == 0)
			? 0
			: -1;
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

		if (get_dbi(ROOM_DB_STATE, txn, &dbi, room_id) == 0) {
			char state_key[] = "m.room.name";
			char state_fallback[] = "m.room.canonical_alias";

			MDB_val value = {0};
			struct matrix_state_event sevent;

			if (((get_str(txn, dbi, state_key, &value)) == 0
				  || (get_str(txn, dbi, state_fallback, &value)) == 0)
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

		if (get_dbi(ROOM_DB_STATE, txn, &dbi, room_id) == 0) {
			char state_key[] = "m.room.topic";

			MDB_val value = {0};
			struct matrix_state_event sevent;

			if ((get_str(txn, dbi, state_key, &value)) == 0
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

	if (!iterator) {
		return -1;
	}

	MDB_val key = {0};
	MDB_val data = {0};

	if ((mdb_cursor_get(iterator->cursor, &key, &data, MDB_NEXT))
		!= MDB_SUCCESS) {
		return -1;
	}

	return iterator->iter_cb(iterator, &key, &data);
}

struct room_info *
cache_room_info(struct cache *cache, const char *room_id) {
	assert(cache);
	assert(room_id);

	struct room_info *info = malloc(sizeof(*info));

	if (info) {
		MDB_txn *txn = get_txn(cache, MDB_RDONLY);

		if (txn) {
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

static int
cache_rooms_next(struct cache_iterator *iterator, MDB_val *key, MDB_val *data) {
	assert(iterator);
	assert(key);
	assert(data);
	assert(key->mv_data);

	return (*((char **) iterator->data) = strndup(key->mv_data, key->mv_size))
		   ? 0
		   : -1;
}

int
cache_rooms_iterator(
  struct cache *cache, struct cache_iterator *iterator, char **id) {
	assert(cache);
	assert(iterator);

	return cache_iterator_init(
	  cache, iterator, cache->dbs[DB_ROOMS], MDB_RDONLY, cache_rooms_next, id);
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
	  .txn = get_txn(cache, 0),
	  .cache = cache};

	return txn->txn ? 0 : -1;
}

void
cache_save_txn_finish(struct cache_save_txn *txn) {
	if (txn) {
		mdb_txn_commit(txn->txn);
	}
}

int
cache_set_room_dbs(struct cache_save_txn *txn, struct matrix_room *room) {
	if (!txn || !room) {
		return -1;
	}

	for (enum room_db i = 0; i < ROOM_DB_MAX; i++) {
		if ((get_dbi(i, txn->txn, &txn->dbs[i], room->id)) == -1) {
			return -1;
		}
	}

	if (room->type == MATRIX_ROOM_LEAVE) {
		for (enum room_db i = 0; i < ROOM_DB_MAX; i++) {
			mdb_drop(txn->txn, txn->dbs[i], true);
		}

		return -1;
	}

	MDB_cursor *cursor = NULL;

	if ((mdb_cursor_open(txn->txn, txn->dbs[ROOM_DB_ORDER_TO_EVENTS], &cursor))
		== MDB_SUCCESS) {
		MDB_val key = {0};
		MDB_val val = {0};

		if ((mdb_cursor_get(cursor, &key, &val, MDB_LAST)) == MDB_SUCCESS) {
			assert(key.mv_size == sizeof(txn->index));
			if (key.mv_size == sizeof(txn->index)) {
				memcpy(&txn->index, key.mv_data, sizeof(txn->index));
			} else {
				/* TODO what to do here? */
				mdb_cursor_close(cursor);
				return -1;
			}

			txn->index++; /* Don't overwrite the last event. */
		}

		mdb_cursor_close(cursor);
	}

	return 0;
}

int
cache_save_room(struct cache_save_txn *txn, struct matrix_room *room) {
	if (!txn || !room) {
		return -1;
	}

	/* TODO dump summary. */
	return put_str(
	  txn->txn, txn->cache->dbs[DB_ROOMS], room->id, (char[]) {""}, 0);
}

enum cache_save_error
cache_save_event(struct cache_save_txn *txn, struct matrix_sync_event *event) {
	if (!txn || !event) {
		return CACHE_FAIL;
	}

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
					== 0) {
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
					== 0) {
					(void) value;
					/* TODO warn about duplicates. */
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
