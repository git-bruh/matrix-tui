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
	/* Map space id to child ids */
	DB_SPACE_CHILDREN,
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
	/* [1, 2, 3, ...] => [Event ID, ...] */
	ROOM_DB_ORDER_TO_EVENTS,
	/* [Event ID, ...] => [1, 2, 3, ...] */
	ROOM_DB_EVENTS_TO_ORDER,
	/* Event ID => [Event ID, ...] Array of related events. */
	ROOM_DB_RELATIONS,
	/* "@username:server.tld" => JSON */
	ROOM_DB_MEMBERS,
	/* "m.room.type" => JSON */
	ROOM_DB_STATE,
	/* "!room_id:server.tld" => JSON */
	ROOM_DB_SPACE_PARENT,
	/* "!room_id:server.tld" => JSON */
	ROOM_DB_SPACE_CHILD,
	ROOM_DB_MAX,
};

enum cache_save_error {
	CACHE_EVENT_SAVED = 0,
	CACHE_EVENT_IGNORED,
	CACHE_EVENT_DEFERRED
};

struct cache {
	MDB_env *env;
	MDB_dbi dbs[DB_MAX];
};

struct cache_iterator_event {
	uint64_t index;
	struct matrix_sync_event event;
};

struct cache_iterator_member {
	char *mxid;
	char *username;
};

struct cache_iterator_space;

struct cache_iterator {
	enum cache_iterator_type {
		CACHE_ITERATOR_ROOMS = 0,
		CACHE_ITERATOR_EVENTS,
		CACHE_ITERATOR_MEMBER,
		CACHE_ITERATOR_SPACES,
		CACHE_ITERATOR_SPACE_CHILDREN,
		CACHE_ITERATOR_MAX
	} type;
	MDB_txn *txn;
	MDB_cursor *cursor;
	struct cache *cache;
	union {
		const char **room_id;
		struct {
			bool fetched_once;
			MDB_dbi events_dbi;
			unsigned timeline_events;
			unsigned state_events;
			uint64_t num_fetch;
			struct cache_iterator_event *event;
			matrix_json_t *event_json;
		};
		struct {
			struct cache_iterator_member *member;
			matrix_json_t *member_json;
		};
		struct cache_iterator_space *space;
		struct {
			bool child_iterated_once;
			char **child_id;
		};
	};
};

struct cache_iterator_space {
	char *id;
	char *child_id;
	struct cache_iterator children_iterator;
};

struct cache_save_txn {
	MDB_dbi dbs[ROOM_DB_MAX];
	uint64_t index;
	const char *room_id;
	MDB_txn *txn;
	struct cache *cache;
};

struct room_info {
	bool invite;
	bool is_space;
	char *name;
	char *topic;
};

enum cache_deferred_ret {
	CACHE_DEFERRED_FAIL = 0,
	CACHE_DEFERRED_ADDED,	/* Child added */
	CACHE_DEFERRED_REMOVED, /* Child removed */
};

/* These events must be processed after the whole sync response as they rely
 * on the state of other rooms. */
struct cache_deferred_space_event {
	bool via_was_null; /* Relation should be broken. */
	enum matrix_state_type type;
	const char *parent;
	const char *child;
	const char *sender;
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
cache_save_txn_init(
  struct cache *cache, struct cache_save_txn *txn, const char *room_id);
void
cache_save_txn_finish(struct cache_save_txn *txn);
int
cache_set_room_dbs(struct cache_save_txn *txn, struct matrix_room *room);
int
cache_save_room(struct cache_save_txn *txn, struct matrix_room *room);
/* Returns 0 if the intended operation was possible, else -1 */
enum cache_deferred_ret
cache_process_deferred_event(
  struct cache *cache, struct cache_deferred_space_event *deferred_event);
enum cache_save_error
cache_save_event(struct cache_save_txn *txn, struct matrix_sync_event *event,
  uint64_t *index, uint64_t *redaction_index,
  struct cache_deferred_space_event **deferred_events);
int
cache_iterator_next(struct cache_iterator *iterator);
void
cache_iterator_finish(struct cache_iterator *iterator);
/* *room_id stores the ID of each room after an iteration. */
int
cache_iterator_rooms(
  struct cache *cache, struct cache_iterator *iterator, const char **room_id);
/* Fetch num_fetch events, starting from end_index and going backwards.
 * end_index == (uint64_t) -1 means start from end.
 * timeline_events and state_events are the result of bitwise OR-ing
 * the type of events that should be iterated over.
 * The events are stored in *event. */
int
cache_iterator_events(struct cache *cache, struct cache_iterator *iterator,
  const char *room_id, struct cache_iterator_event *event, uint64_t end_index,
  uint64_t num_fetch, unsigned timeline_events, unsigned state_events);
/* Member stored in *member. */
int
cache_iterator_member(struct cache *cache, struct cache_iterator *iterator,
  const char *room_id, struct cache_iterator_member *member);
/* Space stored in *space, has a nested iterator spaces->children_iterator for
 * child spaces. */
int
cache_iterator_spaces(struct cache *cache, struct cache_iterator *iterator,
  struct cache_iterator_space *space);
int
cache_room_info_init(
  struct cache *cache, struct room_info *info, const char *room_id);
void
cache_room_info_finish(struct room_info *info);
/* Hack for MDB APIs that don't mark their args as const. */
char *
noconst(const char *str);
#endif /* !CACHE_H */
