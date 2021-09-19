#ifndef MATRIX_H
#define MATRIX_H
#include <ev.h>
/* Must allocate enum + 1. */
enum matrix_limits {
	MATRIX_MXID_MAX = 255,
};

enum matrix_room_status {
	MATRIX_ROOM_JOINED = 0,
	MATRIX_ROOM_INVITED,
	MATRIX_ROOM_LEFT,
};

struct matrix;

struct matrix_room {
	char *id;
	/* TODO summary. */
};

struct matrix_state_event {};

struct matrix_timeline_event {
	struct {
		char *body;
		char *formatted_body;
	} content;
	char *sender;
	char *type;
	char *event_id;
};

struct matrix_ephemeral_event {};
struct matrix_account_data {};

/* Any data received from these callbacks (except userp) _SHOULD_ be treated as
 * read-only. Users should create a local copy of the data when required instead
 * of storing the returned pointers. */
struct matrix_callbacks {
	void (*on_login)(struct matrix *matrix, const char *access_token,
	                 void *userp);
	/* This callback is called to notify the application of the room that is
	 * currently being processed. Events present in subsequent invokations of
	 * the on_room_event callback belong to this room. */
	void (*on_room)(struct matrix *matrix, const struct matrix_room *room,
	                enum matrix_room_status status, void *userp);
	void (*on_state_event)(struct matrix *matrix,
	                       const struct matrix_state_event *event, void *userp);
	void (*on_timeline_event)(struct matrix *matrix,
	                          const struct matrix_timeline_event *event,
	                          void *userp);
	void (*on_ephemeral_event)(struct matrix *matrix,
	                           const struct matrix_ephemeral_event *event,
	                           void *userp);
	void (*on_account_data_event)(
		struct matrix *matrix, const struct matrix_account_data *account_data,
		void *userp);
};

/* Returns NULL on failure. */
struct matrix *
matrix_alloc(struct ev_loop *loop, struct matrix_callbacks callbacks,
             const char *mxid, const char *homeserver, void *userp);
void
matrix_destroy(struct matrix *matrix);

/* These functions return -1 on failure due to allocation failure / invalid
 * arguments and 0 on success. */

/* nullable: device_id */
int
matrix_login(struct matrix *matrix, const char *password,
             const char *device_id);
/* timeout specifies the number of seconds to wait for before syncing again.
 * timeout >= 1 && timeout <= 60 */
int
matrix_sync(struct matrix *matrix, int timeout);
#endif /* !MATRIX_H */
