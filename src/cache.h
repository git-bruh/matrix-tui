/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "matrix.h"

#include <lmdb.h>
#include <stdbool.h>

enum db {
	/* Access token, prev/next batch. */
	DB_SYNC = 0,
	/* Room info. */
	DB_ROOMS,
	DB_MAX,
};

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

enum cache_save_error { CACHE_FAIL = -1, CACHE_SUCCESS, CACHE_GOT_REDACTION };

struct cache {
	MDB_env *env;
	MDB_dbi dbs[DB_MAX];
};

/* A limited iterator interface that just allows returning heap-allocated data.
 */
struct cache_iterator {
	MDB_txn *txn;
	MDB_cursor *cursor;
	struct cache *cache;
	void *data; /* The data that will be filled with contents. */
	int (*iter_cb)(
	  struct cache_iterator *iterator, MDB_val *key, MDB_val *data);
};

struct cache_save_txn {
	uint64_t index;
	uint64_t latest_redaction; /* Valid if cache_save_event returns 1. */
	MDB_txn *txn;
	struct cache *cache;
	MDB_dbi dbs[ROOM_DB_MAX];
};

struct room_info {
	bool invite;
	bool space;
	char *name;
	char *topic;
	/* char *version; */
};

int
cache_init(struct cache *cache);
void
cache_finish(struct cache *cache);
char *
cache_get_token(struct cache *cache);
int
cache_set_token(struct cache *cache, char *access_token);
char *
cache_next_batch(struct cache *cache);
int
cache_save_txn_init(struct cache *cache, struct cache_save_txn *txn);
void
cache_save_txn_finish(struct cache_save_txn *txn);
int
cache_save_next_batch(struct cache_save_txn *txn, char *next_batch);
int
cache_set_room_dbs(struct cache_save_txn *txn, struct matrix_room *room);
int
cache_save_room(struct cache_save_txn *txn, struct matrix_room *room);
enum cache_save_error
cache_save_event(struct cache_save_txn *txn, struct matrix_sync_event *event);
int
cache_iterator_next(struct cache_iterator *iterator);
void
cache_iterator_finish(struct cache_iterator *iterator);
int
cache_rooms_iterator(
  struct cache *cache, struct cache_iterator *iterator, char **id);
struct room_info *
cache_room_info(struct cache *cache, const char *room_id);
void
room_info_destroy(struct room_info *info);
