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

struct cache {
	MDB_env *env;
	MDB_dbi dbs[DB_MAX];
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
cache_room_name(struct cache *cache, MDB_txn *txn, const char *room_id);
char *
cache_room_topic(struct cache *cache, MDB_txn *txn, const char *room_id);
char *
cache_next_batch(struct cache *cache);
void
cache_save(struct cache *cache, struct matrix_sync_response *response);
