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

enum room_db {
	/* Event ID => JSON */
	ROOM_DB_EVENTS = 0,
	/* [1, 2, 3, ...] => Event ID */
	ROOM_DB_ORDER_TO_EVENTS,
	/* Event ID => [1, 2, 3, ...] */
	ROOM_DB_EVENTS_TO_ORDER,
	/* "@username:server.tld" => JSON */
	ROOM_DB_MEMBERS,
	/* "m.room.type" => JSON */
	ROOM_DB_STATE,
	ROOM_DB_MAX,
};

enum { DB_KEY_ACCESS_TOKEN = 0, DB_KEY_NEXT_BATCH, DB_KEY_MAX };

static const char *const db_names[DB_MAX] = {
  [DB_SYNC] = "sync_state",
  [DB_ROOMS] = "rooms",
};

static const char *const db_keys[DB_KEY_MAX] = {
  [DB_KEY_ACCESS_TOKEN] = "access_token",
  [DB_KEY_NEXT_BATCH] = "next_batch",
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

	if ((mdb_env_create(&cache->env)) == MDB_SUCCESS
		&& (mdb_env_set_maxdbs(cache->env, max_dbs)) == MDB_SUCCESS
		&& (mdb_env_open(cache->env, dir, 0, db_perms)) == MDB_SUCCESS
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
cache_get_token(struct cache *cache) {
	if (!cache) {
		return NULL;
	}

	MDB_txn *txn = get_txn(cache, 0);
	MDB_val data = {0};

	char *ret = NULL;

	if (txn
		&& (get_str(txn, cache->dbs[DB_SYNC],
			 noconst(db_keys[DB_KEY_ACCESS_TOKEN]), &data))
			 == 0) {
		ret = strndup(data.mv_data, data.mv_size);
	}

	mdb_txn_commit(txn);
	return ret;
}

int
cache_set_token(struct cache *cache, char *access_token) {
	int ret = -1;

	if (!cache || !access_token) {
		return ret;
	}

	MDB_txn *txn = NULL;

	if ((txn = get_txn(cache, 0))
		&& (put_str(txn, cache->dbs[DB_SYNC],
			 noconst(db_keys[DB_KEY_ACCESS_TOKEN]), access_token, 0))
			 == 0) {
		ret = 0;
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

char *
cache_next_batch(struct cache *cache) {
	MDB_txn *txn = NULL;

	if (cache && (txn = get_txn(cache, MDB_RDONLY))) {
		MDB_val value = {0};

		if ((get_str(txn, cache->dbs[DB_SYNC],
			  noconst(db_keys[DB_KEY_NEXT_BATCH]), &value))
			== 0) {
			mdb_txn_commit(txn);

			return strndup(value.mv_data, value.mv_size);
		}

		mdb_txn_commit(txn);
	}

	return NULL;
}

void
cache_save(struct cache *cache, struct matrix_sync_response *response) {
	if (!cache || !response) {
		return;
	}

	assert(cache->env);

	MDB_txn *txn = NULL;

	if ((txn = get_txn(cache, 0))
		&& (put_str(txn, cache->dbs[DB_SYNC],
			 noconst(db_keys[DB_KEY_NEXT_BATCH]), response->next_batch, 0))
			 == 0) {
		struct matrix_room room;

		while ((matrix_sync_room_next(response, &room)) == 0) {
			bool fail = false;
			MDB_dbi dbs[ROOM_DB_MAX] = {0};

			for (enum room_db i = 0; i < ROOM_DB_MAX; i++) {
				if ((get_dbi(i, txn, &dbs[i], room.id)) == -1) {
					fail = true;
					break;
				}
			}

			if (fail) {
				continue;
			}

			if (room.type == MATRIX_ROOM_LEAVE) {
				for (enum room_db i = 0; i < ROOM_DB_MAX; i++) {
					mdb_drop(txn, dbs[i], true);
				}

				continue;
			}

			/* Start in the middle so we can easily backfill while filling in
			 * events forward aswell. */
			uint64_t index = UINT64_MAX / 2;

			MDB_cursor *cursor = NULL;

			if ((mdb_cursor_open(txn, dbs[ROOM_DB_ORDER_TO_EVENTS], &cursor))
				== MDB_SUCCESS) {
				MDB_val key = {0};
				MDB_val val = {0};

				if ((mdb_cursor_get(cursor, &key, &val, MDB_LAST))
					== MDB_SUCCESS) {
					assert(key.mv_size == sizeof(index));
					if (key.mv_size == sizeof(index)) {
						memcpy(&index, key.mv_data, sizeof(index));
					} else {
						/* TODO what to do here? */
						mdb_cursor_close(cursor);
						continue;
					}
					index++; /* Don't overwrite the last event. */
				}

				mdb_cursor_close(cursor);
			}

			struct matrix_sync_event event;

			while ((matrix_sync_event_next(&room, &event)) == 0) {
				switch (event.type) {
				case MATRIX_EVENT_STATE:
					{
						struct matrix_state_event *sevent = &event.state;

						if (sevent->type == MATRIX_ROOM_MEMBER) {
							assert(sevent->state_key);

							if (!sevent->state_key) {
								continue;
							}

							if ((STREQ(sevent->member.membership, "leave"))) {
								del_str(
								  txn, dbs[ROOM_DB_MEMBERS], sevent->state_key);
							} else {
								char *data = matrix_json_print(event.json);
								put_str(txn, dbs[ROOM_DB_MEMBERS],
								  sevent->state_key, data, 0);
								free(data);
							}
						} else if (!sevent->state_key
								   || (strnlen(sevent->state_key, 1)) == 0) {
							char *data = matrix_json_print(event.json);
							put_str(txn, dbs[ROOM_DB_STATE], sevent->base.type,
							  data, 0);
							free(data);
						}
					}
					break;
				case MATRIX_EVENT_TIMELINE:
					{
						struct matrix_timeline_event *tevent = &event.timeline;

						if (tevent->type == MATRIX_ROOM_REDACTION) {
							MDB_val del_index = {0};

							if ((get_str(txn, dbs[ROOM_DB_EVENTS_TO_ORDER],
								  tevent->base.event_id, &del_index))
								== 0) {
								mdb_del(txn, dbs[ROOM_DB_ORDER_TO_EVENTS],
								  &del_index, NULL);
							}

							del_str(txn, dbs[ROOM_DB_EVENTS_TO_ORDER],
							  tevent->base.event_id);
							del_str(
							  txn, dbs[ROOM_DB_EVENTS], tevent->base.event_id);
						} else {
							MDB_val value = {0};

							if ((get_str(txn, dbs[ROOM_DB_EVENTS],
								  tevent->base.event_id, &value))
								== 0) {
								(void) value;
								/* TODO warn */
								continue;
							}

							char *data = matrix_json_print(event.json);
							put_int(txn, dbs[ROOM_DB_ORDER_TO_EVENTS], index,
							  tevent->base.event_id, 0);
							put_str_int(txn, dbs[ROOM_DB_EVENTS_TO_ORDER],
							  tevent->base.event_id, index, 0);
							put_str(txn, dbs[ROOM_DB_EVENTS],
							  tevent->base.event_id, data, 0);
							index++;

							free(data);
						}
					}
					break;
				default:
					break;
				}
			}

			struct room_info info = {
			  .name = cache_room_name(cache, txn, room.id),
			  .topic = cache_room_topic(cache, txn, room.id),
			};

			free(info.name);
			free(info.topic);
		}
	}

	mdb_txn_commit(txn);
}
