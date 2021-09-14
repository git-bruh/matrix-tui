#ifndef MATRIX_H
#define MATRIX_H
#include <ev.h>

struct matrix;

struct matrix_room_event {
	struct {
		char *body;
		char *formatted_body;
	} content;
	char *sender;
	char *type;
	char *event_id;
	char *room_id;
};

/* Any data received from these callbacks (except userp) _SHOULD_ be treated as
 * read-only, users should create a local copy of the data when required instead
 * of storing the returned pointers. */
struct matrix_callbacks {
	void (*on_login)(struct matrix *matrix, const char *access_token,
	                 void *userp);
	void (*on_room_event)(struct matrix *matrix,
	                      const struct matrix_room_event *event, void *userp);
};

/* Must allocate enum + 1. */
enum {
	MATRIX_MXID_MAX = 255,
};

enum matrix_type {
	MATRIX_SYNC = -1, /* Internal. */
	MATRIX_LOGIN = 0,
};

struct matrix *
matrix_alloc(struct ev_loop *loop, struct matrix_callbacks callbacks,
             const char *mxid, const char *homeserver, void *userp);
void
matrix_destroy(struct matrix *matrix);

int
matrix_login(struct matrix *matrix, const char *password,
             const char *device_id);
int
matrix_sync(struct matrix *matrix, int timeout);
#endif /* !MATRIX_H */
