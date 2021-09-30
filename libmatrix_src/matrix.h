#pragma once
#include "iterator.h"
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
		unsigned joined_member_count;
		unsigned invited_member_count;
		size_t len_heroes;
		char **heroes;
	} summary;
};

struct matrix_file_info {
	unsigned size;
	char *mimetype; /* nullable. */
};

#define MATRIX_EVENT_BASEFIELDS                                                \
	unsigned origin_server_ts;                                                 \
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
	char *user_id;
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
	unsigned ban;
	unsigned events_default;
	unsigned invite;
	unsigned kick;
	unsigned redact;
	unsigned state_default;
	unsigned users_default;
	void *events; /* TODO Hashtables. */
	void *users;
	void *notifications;
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
struct matrix_callbacks {
	void (*login)(struct matrix *matrix, const char *access_token, void *userp);
	/* Gives information about the sync response aswell as the room from which
	 * the events are being dispatched from. */
	void (*dispatch_start)(struct matrix *matrix,
						   const struct matrix_dispatch_info *info,
						   void *userp);
	void (*typing)(struct matrix *matrix,
				   const struct matrix_room_typing *typing, void *userp);
	void (*avatar)(struct matrix *matrix,
				   const struct matrix_room_avatar *avatar, void *userp);
	void (*topic)(struct matrix *matrix, const struct matrix_room_topic *topic,
				  void *userp);
	void (*name)(struct matrix *matrix, const struct matrix_room_name *name,
				 void *userp);
	void (*power_levels)(struct matrix *matrix,
						 const struct matrix_room_power_levels *power_levels,
						 void *userp);
	void (*member)(struct matrix *matrix,
				   const struct matrix_room_member *member, void *userp);
	void (*join_rules)(struct matrix *matrix,
					   const struct matrix_room_join_rules *join_rules,
					   void *userp);
	void (*room_create)(struct matrix *matrix,
						const struct matrix_room_create *room_create,
						void *userp);
	void (*canonical_alias)(
		struct matrix *matrix,
		const struct matrix_room_canonical_alias *canonical_alias, void *userp);
	void (*unknown_state)(struct matrix *matrix,
						  const struct matrix_unknown_state *unknown_state,
						  void *userp);
	void (*message)(struct matrix *matrix,
					const struct matrix_room_message *message, void *userp);
	void (*redaction)(struct matrix *matrix,
					  const struct matrix_room_redaction *redaction,
					  void *userp);
	void (*attachment)(struct matrix *matrix,
					   const struct matrix_room_attachment *attachment,
					   void *userp);
	/* Called once all events for a given room are consumed, does not indicate
	 * end of sync parsing. */
	void (*dispatch_end)(struct matrix *matrix, void *userp);
};

/* Returns NULL on failure, must call matrix_global_init() before anything. */
struct matrix *
matrix_alloc(struct ev_loop *loop, const struct matrix_callbacks callbacks,
			 const char *mxid, const char *homeserver, void *userp);
void
matrix_destroy(struct matrix *matrix);
void
matrix_global_cleanup(void);

/* These functions return -1 on failure due to allocation failure / invalid
 * arguments and 0 on success. */
int
matrix_global_init(void);
/* nullable: device_id */
int
matrix_login(struct matrix *matrix, const char *password,
			 const char *device_id);
/* timeout specifies the number of seconds to wait for before syncing again.
 * timeout >= 1 && timeout <= 60 */
int
matrix_sync(struct matrix *matrix, int timeout);
