#ifndef CACHE_H
#define CACHE_H
/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "matrix.h"

#include <lmdb.h>
#include <stdbool.h>

enum db {
	/* Access token, prev/next batch, MXID, Homeserver. */
	DB_AUTH = 0,
	/* Room info. */
	DB_ROOMS,
	DB_MAX,
};

enum auth_key {
	DB_KEY_ACCESS_TOKEN = 0,
	DB_KEY_NEXT_BATCH,
	DB_KEY_MXID,
	DB_KEY_HOMESERVER,
	DB_KEY_MAX
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

struct cache_iterator_event {
	uint64_t index;
	struct matrix_timeline_event event;
	matrix_json_t *json;
};

struct cache_iterator_member {
	char *mxid;
	char *username;
	matrix_json_t *json;
};

struct cache_iterator {
	enum cache_iterator_type {
		CACHE_ITERATOR_ROOMS = 0,
		CACHE_ITERATOR_EVENTS,
		CACHE_ITERATOR_MEMBER,
		CACHE_ITERATOR_MAX
	} type;
	MDB_txn *txn;
	MDB_cursor *cursor;
	struct cache *cache;
	union {
		char **room_id;
		struct {
			struct cache_iterator_event *event;
			MDB_dbi events_to_order;
		};
		struct cache_iterator_member *member;
	};
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
cache_auth_get(struct cache *cache, enum auth_key key);
int
cache_auth_set(struct cache *cache, enum auth_key key, char *auth);
int
cache_save_txn_init(struct cache *cache, struct cache_save_txn *txn);
void
cache_save_txn_finish(struct cache_save_txn *txn);
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
cache_iterator_rooms(
  struct cache *cache, struct cache_iterator *iterator, char **room_id);
int
cache_iterator_events(struct cache *cache, struct cache_iterator *iterator,
  const char *room_id, struct cache_iterator_event *event);
int
cache_iterator_member(struct cache *cache, struct cache_iterator *iterator,
  const char *room_id, struct cache_iterator_member *member);
struct room_info *
cache_room_info(struct cache *cache, const char *room_id);
void
room_info_destroy(struct room_info *info);
#endif /* !CACHE_H */
