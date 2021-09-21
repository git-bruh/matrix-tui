#ifndef MATRIX_H
#define MATRIX_H
#include <ev.h>
#include <stdbool.h>
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

/* All members in these structs are non-nullable unless explicitly mentioned. */

struct matrix_room {
	char *id;
	struct {
		char **heroes;
		size_t len_heroes;
		int joined_member_count;
		int invited_member_count;
	} summary;
};

struct matrix_state_content {
	bool is_direct;	   /* false if not present in the response. */
	char *avatar_url;  /* nullable. */
	char *displayname; /* nullable. */
					   // enum matrix_membership membership;
};

struct matrix_state_event {
	struct matrix_state_content *content;
	struct matrix_state_content *prev_content; /* nullable. */
	char *type;
	char *event_id;
	char *sender;
	char *state_key;
};

struct matrix_timeline_event {
	struct {
		char *body;
		char *formatted_body; /* nullable. */
	} content;
	char *sender;
	char *type;
	char *event_id;
};

struct matrix_ephemeral_event {};

/* TODO confirm where next/prev batch fields can be present. */
struct matrix_dispatch_info {
	struct matrix_room room; /* The current room. */
	struct {
		bool limited;
		char *prev_batch; /* nullable. */
	} timeline;			  /* The current room's timeline. */
	/* These fields correspond to the whole sync response and not the current
	 * room's timeline. */
	char *prev_batch; /* nullable. */
	char *next_batch; /* nullable. */
};

/* Any data received from these callbacks (except userp) _SHOULD_ be treated as
 * read-only. Users should create a local copy of the data when required instead
 * of storing the returned pointers. */
struct matrix_callbacks {
	void (*on_login)(struct matrix *matrix, const char *access_token,
					 void *userp);
	/* Gives information about the sync response aswell as the room from which
	 * the events are being dispatched from. */
	void (*on_dispatch_start)(struct matrix *matrix,
							  const struct matrix_dispatch_info *info,
							  void *userp);
	void (*on_state_event)(struct matrix *matrix,
						   const struct matrix_state_event *event, void *userp);
	void (*on_timeline_event)(struct matrix *matrix,
							  const struct matrix_timeline_event *event,
							  void *userp);
	void (*on_ephemeral_event)(struct matrix *matrix,
							   const struct matrix_ephemeral_event *event,
							   void *userp);
	/* Called once all events for a given room are consumed, does not indicate
	 * end of sync parsing. */
	void (*on_dispatch_end)(struct matrix *matrix, void *userp);
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
