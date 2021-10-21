#ifndef MATRIX_MATRIX_H
#define MATRIX_MATRIX_H
#include <stdbool.h>
/* Must allocate enum + 1. */
enum matrix_limits {
	MATRIX_MXID_MAX = 255,
};

enum matrix_code {
	MATRIX_SUCCESS = 0,
	MATRIX_NOMEM,
	MATRIX_CURL_FAILURE,
	MATRIX_MALFORMED_JSON,
	MATRIX_INVALID_ARGUMENT,
	MATRIX_NOT_LOGGED_IN,
};

struct matrix;

typedef struct cJSON matrix_json_t;

/* Members of all structs are non-nullable unless explicitly mentioned. */
/* The "base" members of all event structs. */
struct matrix_state_base {
	char *event_id;
	char *sender;
	char *type;
	char *state_key;
	int origin_server_ts;
};

struct matrix_room_base {
	char *event_id;
	char *sender;
	char *type;
	int origin_server_ts;
};

struct matrix_ephemeral_base {
	char *type;
	char *room_id;
};

struct matrix_file_info {
	int size;
	char *mimetype; /* nullable. */
};

struct matrix_room_typing {
	matrix_json_t *user_ids;
	struct matrix_ephemeral_base base;
};

struct matrix_room_canonical_alias {
	char *alias; /* nullable. */
	struct matrix_state_base base;
};

struct matrix_room_create {
	bool federate;
	char *creator;
	const char
		*room_version; /* This is marked const as we assign a string literal to
						  it if the room_version key is not present. */
	struct matrix_state_base base;
};

struct matrix_room_join_rules {
	char *join_rule;
	struct matrix_state_base base;
};

struct matrix_room_member {
	bool is_direct;
	char *membership;
	char *prev_membership; /* nullable. */
	char *avatar_url;	   /* nullable. */
	char *displayname;	   /* nullable. */
	struct matrix_state_base base;
};

struct matrix_room_power_levels {
	int ban;
	int events_default;
	int invite;
	int kick;
	int redact;
	int state_default;
	int users_default;
	matrix_json_t *events;		  /* nullable. */
	matrix_json_t *users;		  /* nullable. */
	matrix_json_t *notifications; /* nullable. */
	struct matrix_state_base base;
};

struct matrix_room_name {
	char *name;
	struct matrix_state_base base;
};

struct matrix_room_topic {
	char *topic;
	struct matrix_state_base base;
};

struct matrix_room_avatar {
	char *url;
	struct matrix_file_info info;
	struct matrix_state_base base;
};

struct matrix_unknown_state {
	matrix_json_t *content;
	matrix_json_t *prev_content; /* nullable. */
	struct matrix_state_base base;
};

struct matrix_room_message {
	char *body;
	char *msgtype;
	char *format;		  /* nullable. */
	char *formatted_body; /* nullable. */
	struct matrix_room_base base;
};

struct matrix_room_redaction {
	char *redacts;
	char *reason; /* nullable. */
	struct matrix_room_base base;
};

struct matrix_room_attachment {
	char *body;
	char *msgtype;
	char *url;
	char *filename;
	struct matrix_file_info info;
	struct matrix_room_base base;
};

struct matrix_room_summary {
	int joined_member_count;
	int invited_member_count;
	matrix_json_t *heroes; /* TODO somehow abstract the cJSON object away:
					  1. Make an iterator with (void *)
					  2. Dump raw JSON as (char *)
					  3. Maybe just don't */
};

struct matrix_room_timeline {
	char *prev_batch;
	bool limited;
};

enum matrix_event_type {
	/* TODO account_data */
	MATRIX_EVENT_STATE = 0,
	MATRIX_EVENT_TIMELINE,
	MATRIX_EVENT_EPHEMERAL,
	MATRIX_EVENT_MAX,
};

struct matrix_room {
	char *id;
	matrix_json_t *events[MATRIX_EVENT_MAX];
	struct matrix_room_summary summary;
	struct matrix_room_timeline
		timeline; /* Irrelevant if type == MATRIX_ROOM_INVITE. */
	enum matrix_room_type {
		MATRIX_ROOM_LEAVE = 0,
		MATRIX_ROOM_JOIN,
		MATRIX_ROOM_INVITE,
		MATRIX_ROOM_MAX
	} type;
};

struct matrix_sync_response {
	char *next_batch;
	matrix_json_t *rooms[MATRIX_ROOM_MAX];
	/* struct matrix_account_data_events account_data; */
};

struct matrix_state_event {
	enum matrix_state_type {
		MATRIX_ROOM_MEMBER = 0,
		MATRIX_ROOM_POWER_LEVELS,
		MATRIX_ROOM_CANONICAL_ALIAS,
		MATRIX_ROOM_CREATE,
		MATRIX_ROOM_JOIN_RULES,
		MATRIX_ROOM_NAME,
		MATRIX_ROOM_TOPIC,
		MATRIX_ROOM_AVATAR,
		MATRIX_ROOM_UNKNOWN_STATE,
	} type;
	union {
		struct matrix_room_member member;
		struct matrix_room_power_levels power_levels;
		struct matrix_room_canonical_alias canonical_alias;
		struct matrix_room_create create;
		struct matrix_room_join_rules join_rules;
		struct matrix_room_name name;
		struct matrix_room_topic topic;
		struct matrix_room_avatar avatar;
		struct matrix_unknown_state unknown_state;
	};
};

struct matrix_timeline_event {
	enum matrix_timeline_type {
		MATRIX_ROOM_MESSAGE = 0,
		MATRIX_ROOM_REDACTION,
		MATRIX_ROOM_ATTACHMENT,
	} type;
	union {
		struct matrix_room_message message;
		struct matrix_room_redaction redaction;
		struct matrix_room_attachment attachment;
	};
};

struct matrix_ephemeral_event {
	enum matrix_ephemeral_type {
		MATRIX_ROOM_TYPING = 0,
	} type;
	union {
		struct matrix_room_typing typing;
	};
};

typedef void (*matrix_sync_cb)(struct matrix *, struct matrix_sync_response *);

/* Functions returning int (Except enums) return -1 on failure and 0 on success.
 * Functions returning pointers return NULL on failure. */

/* ALLOC/DESTROY */

/* Must be the first function called only a single time. */
int
matrix_global_init(void);
struct matrix *
matrix_alloc(matrix_sync_cb sync_cb, const char *mxid, const char *homeserver,
			 void *userp);
void
matrix_destroy(struct matrix *matrix);
/* Must be the last function called only a single time. */
void
matrix_global_cleanup(void);

/* SYNC */

enum matrix_code
matrix_login_with_token(struct matrix *matrix, const char *access_token);
/* nullable: device_id */
enum matrix_code
matrix_login(struct matrix *matrix, const char *password,
			 const char *device_id);

/* timeout specifies the maximum time in milliseconds that the server will wait
 * for events to be received. The recommended minimum is 1000 == 1 second to
 * avoid burning CPU cycles. */
/* nullable: next_batch */
enum matrix_code
matrix_sync_forever(struct matrix *matrix, const char *next_batch,
					unsigned timeout);

/* These functions fill in the passed struct with the corresponding JSON item's
 * representation at the current index. */
int
matrix_sync_room_next(struct matrix_sync_response *response,
					  struct matrix_room *room);
int
matrix_sync_state_next(struct matrix_room *room,
					   struct matrix_state_event *event);
int
matrix_sync_timeline_next(struct matrix_room *room,
						  struct matrix_timeline_event *event);
int
matrix_sync_ephemeral_next(struct matrix_room *room,
						   struct matrix_ephemeral_event *event);

/* Magic macro to call one of the above functions depending on the argument
 * type. */
#define matrix_sync_next(response_or_room, result)                             \
	_Generic((result), struct matrix_room *                                    \
			 : matrix_sync_room_next, struct matrix_state_event *              \
			 : matrix_sync_state_next, struct matrix_timeline_event *          \
			 : matrix_sync_timeline_next, struct matrix_ephemeral_event *      \
			 : matrix_sync_ephemeral_next)(response_or_room, result)

/* API */

#endif /* !MATRIX_MATRIX_H */
