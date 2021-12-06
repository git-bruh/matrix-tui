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

struct cache {
	MDB_env *env;
	MDB_dbi dbs[DB_MAX];
};

struct cache_save_txn {
	uint64_t index;
	MDB_txn *txn;
	struct cache *cache;
	MDB_dbi dbs[ROOM_DB_MAX];
};

struct room_info {
	bool invite;
	bool space;
	char *name;
	char *topic;
	char *version;
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
cache_room_name(struct cache *cache, MDB_txn *txn, const char *room_id);
char *
cache_room_topic(struct cache *cache, MDB_txn *txn, const char *room_id);
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
cache_save_event(struct cache_save_txn *txn, struct matrix_sync_event *event);
