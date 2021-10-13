#pragma once
#include "iterator.h"
#include <stdbool.h>
/* Must allocate enum + 1. */
enum matrix_limits {
	MATRIX_MXID_MAX = 255,
};

enum matrix_code {
	MATRIX_SUCCESS = 0,
	MATRIX_CURL_FAILURE,
	MATRIX_NOMEM,
	MATRIX_INVALID_ARGUMENT,
	MATRIX_NOT_LOGGED_IN,
};

struct matrix;

/* All members in these structs are non-nullable unless explicitly mentioned. */

struct matrix_room {
	char *id;
	struct {
		int joined_member_count;
		int invited_member_count;
		matrix_iterator_t *heroes;
	} summary;
};

struct matrix_file_info {
	int size;
	char *mimetype; /* nullable. */
};

#define MATRIX_EVENT_BASEFIELDS                                                \
	int origin_server_ts;                                                      \
	char *event_id;                                                            \
	char *sender;                                                              \
	char *type

struct matrix_state_base {
	MATRIX_EVENT_BASEFIELDS;
	char *state_key;
};

struct matrix_room_base {
	MATRIX_EVENT_BASEFIELDS;
};

#undef MATRIX_EVENT_BASEFIELDS

struct matrix_room_typing {
	matrix_iterator_t *users;
};

struct matrix_room_canonical_alias {
	struct matrix_state_base *base;
	char *alias; /* nullable. */
};

struct matrix_room_create {
	bool federate;
	char *creator;
	char *room_version;
	struct matrix_state_base *base;
};

struct matrix_room_join_rules {
	char *join_rule;
	struct matrix_state_base *base;
};

struct matrix_room_member {
	bool is_direct;
	char *membership;
	char *prev_membership; /* nullable. */
	char *avatar_url;	   /* nullable. */
	char *displayname;	   /* nullable. */
	struct matrix_state_base *base;
};

struct matrix_room_power_levels {
	int ban;
	int events_default;
	int invite;
	int kick;
	int redact;
	int state_default;
	int users_default;
	matrix_iterator_t *events;		  /* nullable. */
	matrix_iterator_t *users;		  /* nullable. */
	matrix_iterator_t *notifications; /* nullable. */
	struct matrix_state_base *base;
};

struct matrix_room_name {
	char *name;
	struct matrix_state_base *base;
};

struct matrix_room_topic {
	char *topic;
	struct matrix_state_base *base;
};

struct matrix_room_avatar {
	char *url;
	struct matrix_state_base *base;
	struct matrix_file_info info;
};

struct matrix_unknown_state {
	struct matrix_state_base *base;
	char *content;		/* Raw JSON. */
	char *prev_content; /* nullable, raw JSON. */
};

struct matrix_room_message {
	struct matrix_room_base *base;
	char *body;
	char *msgtype;
	char *format;		  /* nullable. */
	char *formatted_body; /* nullable. */
};

struct matrix_room_redaction {
	struct matrix_room_base *base;
	char *redacts;
	char *reason; /* nullable. */
};

struct matrix_room_attachment {
	struct matrix_room_base *base;
	char *body;
	char *msgtype;
	char *url;
	char *filename;
	struct matrix_file_info info;
};

struct matrix_dispatch_info {
	struct matrix_room room; /* The current room. */
	struct {
		bool limited;
		char *prev_batch; /* nullable. */
	} timeline;			  /* The current room's timeline. */
	char *next_batch;
};

/* Any data received from these callbacks (except userp) _SHOULD_ be treated as
 * read-only. Users should create a local copy of the data when required instead
 * of storing the returned pointers. */
/* TODO deliver the sync response as a large struct instead ? */
struct matrix_callbacks {
	/* Gives information about the sync response aswell as the room from which
	 * the events are being dispatched from. */
	void (*dispatch_start)(struct matrix *matrix,
						   const struct matrix_dispatch_info *info);
	void (*typing)(struct matrix *matrix,
				   const struct matrix_room_typing *typing);
	void (*avatar)(struct matrix *matrix,
				   const struct matrix_room_avatar *avatar);
	void (*topic)(struct matrix *matrix, const struct matrix_room_topic *topic);
	void (*name)(struct matrix *matrix, const struct matrix_room_name *name);
	void (*power_levels)(struct matrix *matrix,
						 const struct matrix_room_power_levels *power_levels);
	void (*member)(struct matrix *matrix,
				   const struct matrix_room_member *member);
	void (*join_rules)(struct matrix *matrix,
					   const struct matrix_room_join_rules *join_rules);
	void (*room_create)(struct matrix *matrix,
						const struct matrix_room_create *room_create);
	void (*canonical_alias)(
		struct matrix *matrix,
		const struct matrix_room_canonical_alias *canonical_alias);
	void (*unknown_state)(struct matrix *matrix,
						  const struct matrix_unknown_state *unknown_state);
	void (*message)(struct matrix *matrix,
					const struct matrix_room_message *message);
	void (*redaction)(struct matrix *matrix,
					  const struct matrix_room_redaction *redaction);
	void (*attachment)(struct matrix *matrix,
					   const struct matrix_room_attachment *attachment);
	/* Called once all events for a given room are consumed, does not indicate
	 * end of sync parsing. */
	void (*dispatch_end)(struct matrix *matrix);
};

/* Returns NULL on failure, must call matrix_global_init() before anything. */
struct matrix *
matrix_alloc(struct matrix_callbacks callbacks, const char *mxid,
			 const char *homeserver, void *userp);
void
matrix_destroy(struct matrix *matrix);
void
matrix_global_cleanup(void);

/* These functions return -1 on failure due to allocation failure / invalid
 * arguments and 0 on success. */
int
matrix_global_init(void);
/* nullable: device_id */
enum matrix_code
matrix_login(struct matrix *matrix, const char *password,
			 const char *device_id);
/* timeout specifies the number of seconds to wait for before syncing again.
 * timeout >= 1 && timeout <= 60 */
/* matrix_sync(); */
