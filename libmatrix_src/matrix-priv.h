#ifndef MATRIX_PRIV_H
#define MATRIX_PRIV_H
#include "matrix.h"
#include <curl/curl.h>
#include <stdbool.h>

struct matrix {
	CURLM *multi;
	struct curl_slist *headers;
	struct ev_loop *loop;
	struct matrix_callbacks cb;
	struct ev_timer timer_event;
	struct ll *ll; /* Doubly linked list to keep track of added handles and
	                  clean them up. */
	int still_running;
	bool authorized;
	char mxid[MATRIX_MXID_MAX + 1];
	char *homeserver;
	void *userp;
};

struct transfer {
	CURL *easy; /* We must keep track of the easy handle even though sock_in
fo
	               has it as transfers might be stopped before any progress
is
	               made on them, and sock_info would be NULL. */
	struct sock_info *sock_info;
	struct {
		char *buf;
		size_t size;
	} mem;
	enum matrix_type type;
	char error[CURL_ERROR_SIZE];
};

extern void (*const matrix_dispatch[MATRIX_NUM_TYPES + 1])(
	struct matrix *matrix, const char *resp);

int
matrix_transfer_add(struct matrix *matrix, CURL *easy, enum matrix_type type);
int
matrix_set_authorization(struct matrix *matrix, const char *token);
void
matrix_dispatch_response(struct matrix *matrix, struct transfer *transfer);
#endif /* !MATRIX_PRIV_H */
