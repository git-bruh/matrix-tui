#pragma once
#include <ev.h>
#include <stdbool.h>
/* Must allocate enum + 1. */
enum matrix_limits {
	MATRIX_MXID_MAX = 255,
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

/* https://github.com/libuv/libuv/blob/bcc4f8fdde45471f30e168fe27be347076ebdf2c/include/uv.h#L399
 */
#define MATRIX_STATE_BASEFIELDS                                                \
	unsigned origin_server_ts;                                                 \
	char *event_id;                                                            \
	char *sender;                                                              \
	char *state_key;                                                           \
	char *type

/* Generate a name_content and name_state struct with the common fields set. */
#define MATRIX_STATE_STRUCTGEN(name)                                           \
	struct name##_content;                                                     \
	struct name##_state {                                                      \
		MATRIX_STATE_BASEFIELDS;                                               \
		struct name##_content *prev_content;                                   \
		struct name##_content *content;                                        \
	};                                                                         \
	struct name##_content /* struct representing the "content" key. */

/* Expands to:
 * struct matrix_room_canonical_alias_content;
 * struct matrix_room_canonical_alias_state {
 *     unsigned origin_server_ts;
 *     ...
 *     struct matrix_room_canonical_alias_content *prev_content;
 *     struct matrix_room_canonical_alias_content *content;
 * };
 * struct matrix_room_canonical_alias_content {
 *     char *alias;
 *     char **alt_aliases;
 *     size_t len_alt_aliases;
 * };
 */

MATRIX_STATE_STRUCTGEN(matrix_room_canonical_alias) {
	char *alias;
	char **alt_aliases;
	size_t len_alt_aliases;
};

MATRIX_STATE_STRUCTGEN(matrix_room_create) {
	bool federate;
	char *creator;
	char *room_version;
	struct {
		char *event_id;
		char *room_id;
	} predecessor;
};

MATRIX_STATE_STRUCTGEN(matrix_room_join_rules) {
	enum matrix_join_rule {
		MATRIX_JOIN_PUBLIC = 0,
		MATRIX_JOIN_KNOCK,
		MATRIX_JOIN_INVITE,
		MATRIX_JOIN_PRIVATE
	} join_rule;
};

MATRIX_STATE_STRUCTGEN(matrix_room_member) {
	bool is_direct;
	enum matrix_membership {
		MATRIX_MEMBERSHIP_INVITE = 0,
		MATRIX_MEMBERSHIP_JOIN,
		MATRIX_MEMBERSHIP_KNOCK,
		MATRIX_MEMBERSHIP_LEAVE,
		MATRIX_MEMBERSHIP_BAN
	} membership;
	char *avatar_url;  /* nullable. */
	char *displayname; /* nullable. */
};

MATRIX_STATE_STRUCTGEN(matrix_room_power_levels) {
	int ban;
	int events_default;
	int invite;
	int kick;
	int redact;
	int state_default;
	int users_default;
	struct {
		int room;
	} notifications;
	struct {
		// Hashmap of user: level
	} users;
};

MATRIX_STATE_STRUCTGEN(matrix_room_name) { char *name; };

MATRIX_STATE_STRUCTGEN(matrix_room_topic) { char *topic; };

MATRIX_STATE_STRUCTGEN(matrix_room_avatar) { char *url; };

MATRIX_STATE_STRUCTGEN(matrix_room_pinned_events) {
	char **pinned;
	size_t len_pinned;
};

#define MATRIX_ROOM_BASEFIELDS                                                 \
	unsigned origin_server_ts;                                                 \
	char *event_id;                                                            \
	char *sender;                                                              \
	char *type

struct matrix_message_event {
	MATRIX_ROOM_BASEFIELDS;
	struct {
		char *body;
		char *msgtype;
		char *format;
		char *formatted_body;
	} content;
};

struct matrix_redaction_event {
	MATRIX_ROOM_BASEFIELDS;
	char *redacts;
	struct {
		char *reason;
	} content;
};

struct matrix_attachment_event {
	MATRIX_ROOM_BASEFIELDS;
	struct {
		char *body;
		char *msgtype;
		char *url;
		char *filename;
		struct {
			size_t size;
			char *mimetype;
		} info;
	} content;
};

struct matrix_ephemeral_event {};

struct matrix_dispatch_info {
	struct matrix_room room; /* The current room. */
	struct {
		bool limited;
		char *prev_batch; /* nullable. */
	} timeline;			  /* The current room's timeline. */
	/* These fields correspond to the whole sync response and not the current
	 * room's timeline. */
	char *next_batch;
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
