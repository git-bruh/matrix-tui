#include "matrix-priv.h"
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>

struct node {
	void *data;
	struct node *next;
	struct node *prev;
};

struct ll {
	struct node *tail;
	void (*free)(void *data);
};

/* Curl callbacks adapted from https://curl.se/libcurl/c/evhiperfifo.html. */
struct sock_info {
	struct ev_io ev;
	struct matrix *matrix;
	CURL *easy;
	curl_socket_t sockfd;
	int action;
	bool evset;
	long timeout;
};

static struct ll *
ll_alloc(void (*free)(void *data)) {
	struct ll *ll = calloc(1, sizeof(*ll));

	if (!ll) {
		return NULL;
	}

	ll->free = free;

	return ll;
}

struct node *
ll_append(struct ll *ll, void *data) {
	struct node *node = calloc(1, sizeof(*node));

	if (!node) {
		return NULL;
	}

	node->data = data;

	if (ll->tail) {
		node->prev = ll->tail;
		ll->tail->next = node;
		ll->tail = node;
	} else {
		ll->tail = node;
		node->prev = node->next = NULL;
	}

	return node;
}

static void
ll_remove(struct ll *ll, struct node *node) {
	if (node->prev) {
		node->prev->next = node->next;
	}

	node->next ? (node->next->prev = node->prev) : (ll->tail = node->prev);

	ll->free(node->data);

	free(node);
}

static void
ll_free(struct ll *ll) {
	if (!ll) {
		return;
	}

	struct node *prev = NULL;

	while (ll->tail) {
		prev = ll->tail->prev;

		ll->free(ll->tail->data);
		free(ll->tail);

		ll->tail = prev;
	}

	free(ll);
}

static void
free_transfer(void *data) {
	struct transfer *transfer = data;

	curl_easy_cleanup(transfer->easy);

	if (transfer->sock_info && transfer->sock_info->evset) {
		ev_io_stop(transfer->sock_info->matrix->loop, &transfer->sock_info->ev);
		free(transfer->sock_info);
	}

	free(transfer->mem.buf);
	free(transfer);
}

static void
check_multi_info(struct matrix *matrix) {
	CURLMsg *msg = NULL;
	int msgs_left = 0;

	struct node *node = NULL;

	while ((msg = curl_multi_info_read(matrix->multi, &msgs_left))) {
		if (msg->msg == CURLMSG_DONE) {
			curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &node);

			assert(node);

			struct transfer *transfer = node->data;

			matrix_dispatch_response(matrix, transfer);

			assert(!transfer->sock_info);
			assert(msg->easy_handle == (transfer->easy));

			curl_multi_remove_handle(matrix->multi, msg->easy_handle);
			ll_remove(matrix->ll, node);
		}
	}
}

static void
event_cb(EV_P_ struct ev_io *w, int revents) {
	struct matrix *matrix = w->data;

	int action = ((revents & EV_READ) ? CURL_POLL_IN : 0) |
				 ((revents & EV_WRITE) ? CURL_POLL_OUT : 0);

	if ((curl_multi_socket_action(matrix->multi, w->fd, action,
								  &matrix->still_running)) != CURLM_OK) {
		return;
	}

	check_multi_info(matrix);

	/* All transfers done, stop the timer. */
	if (matrix->still_running <= 0) {
		ev_timer_stop(matrix->loop, &matrix->timer_event);
	}
}

static void
timer_cb(EV_P_ struct ev_timer *w, int revents) {
	(void) revents;

	struct matrix *matrix = w->data;

	if ((curl_multi_socket_action(matrix->multi, CURL_SOCKET_TIMEOUT, 0,
								  &matrix->still_running)) == CURLM_OK) {
		check_multi_info(matrix);
	}
}

static int
multi_timer_cb(CURLM *multi, long timeout_ms, struct matrix *matrix) {
	(void) multi;

	ev_timer_stop(matrix->loop, &matrix->timer_event);

	/* -1 indicates that we should stop the timer. */
	if (timeout_ms >= 0) {
		double seconds = (double) (timeout_ms / 1000);

		ev_timer_init(&matrix->timer_event, timer_cb, seconds, 0.);
		ev_timer_start(matrix->loop, &matrix->timer_event);
	}

	return 0;
}

static void
remsock(struct sock_info *sock_info, struct matrix *matrix) {
	if (sock_info) {
		if (sock_info->evset) {
			ev_io_stop(matrix->loop, &sock_info->ev);
		}

		free(sock_info);
	}
}

static void
setsock(struct sock_info *sock_info, curl_socket_t sockfd, CURL *easy,
		int action, struct matrix *matrix) {
	int kind = ((action & CURL_POLL_IN) ? EV_READ : 0) |
			   ((action & CURL_POLL_OUT) ? EV_WRITE : 0);

	sock_info->sockfd = sockfd;
	sock_info->action = action;
	sock_info->easy = easy;

	if (sock_info->evset) {
		ev_io_stop(matrix->loop, &sock_info->ev);
	}

	ev_io_init(&sock_info->ev, event_cb, sock_info->sockfd, kind);

	sock_info->ev.data = matrix;
	sock_info->evset = true;

	ev_io_start(matrix->loop, &sock_info->ev);
}

static void
addsock(curl_socket_t sockfd, CURL *easy, int action, struct matrix *matrix) {
	struct sock_info *sock_info = calloc(1, sizeof(*sock_info));

	if (!sock_info) {
		return;
	}

	sock_info->matrix = matrix;

	setsock(sock_info, sockfd, easy, action, matrix);

	curl_multi_assign(matrix->multi, sockfd, sock_info);
}

static int
sock_cb(CURL *easy, curl_socket_t sockfd, int what, void *userp, void *sockp) {
	struct matrix *matrix = userp;
	struct sock_info *sock_info = sockp;

	if (what == CURL_POLL_REMOVE) {
		remsock(sock_info, matrix);

		struct node *node = NULL;

		curl_easy_getinfo(easy, CURLINFO_PRIVATE, &node);

		assert(node);

		((struct transfer *) node->data)->sock_info = NULL;
	} else {
		if (!sock_info) {
			addsock(sockfd, easy, what, matrix);
		} else {
			setsock(sock_info, sockfd, easy, what, matrix);
		}
	}

	return 0;
}

static size_t
write_cb(void *contents, size_t size, size_t nmemb, void *userp) {
	struct transfer *transfer = userp;

	size_t realsize = size * nmemb;

	char *ptr = realloc(transfer->mem.buf, transfer->mem.size + realsize + 1);

	if (!ptr) {
		return 0;
	}

	transfer->mem.buf = ptr;
	memcpy(&(transfer->mem.buf[transfer->mem.size]), contents, realsize);
	transfer->mem.size += realsize;
	transfer->mem.buf[transfer->mem.size] = '\0';

	return realsize;
}

int
matrix_transfer_add(struct matrix *matrix, CURL *easy, enum matrix_type type) {
	struct transfer *transfer = calloc(1, sizeof(*transfer));
	struct node *node = NULL;

	if (transfer) {
		transfer->easy = easy;
		transfer->type = type;

		if ((node = ll_append(matrix->ll, transfer)) &&
			(curl_easy_setopt(easy, CURLOPT_PRIVATE, node)) == CURLE_OK &&
			(curl_easy_setopt(easy, CURLOPT_ERRORBUFFER, transfer->error)) ==
				CURLE_OK &&
			(curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, write_cb)) ==
				CURLE_OK &&
			(curl_easy_setopt(easy, CURLOPT_WRITEDATA, transfer)) == CURLE_OK &&
			(curl_multi_add_handle(matrix->multi, easy)) == CURLM_OK) {
			/* FIXME Without this, after the first transfer is completed,
			 * subsequent transfers get
			 * thier multi_timer_cb called with a value of 0, but no activity
			 * occurs after that. */
			curl_multi_socket_action(matrix->multi, CURL_SOCKET_TIMEOUT, 0,
									 &matrix->still_running);

			return 0;
		}
	}

	if (node) {
		ll_remove(matrix->ll, node);
	} else {
		free(transfer);
		curl_easy_cleanup(easy);
	}

	return -1;
}

int
matrix_header_append(struct matrix *matrix, const char *header) {
	struct curl_slist *tmp = curl_slist_append(matrix->headers, header);

	if (tmp) {
		matrix->headers = tmp;

		return 0;
	}

	return -1;
}

int
matrix_set_authorization(struct matrix *matrix, const char *token) {
	const char auth[] = "Authorization: Bearer ";

	char *header = NULL;

	if ((asprintf(&header, "%s%s", auth, token)) == -1) {
		return -1;
	}

	matrix_header_append(matrix, header);

	matrix->authorized = true;

	free(header);

	return 0;
}

struct matrix *
matrix_alloc(struct ev_loop *loop, struct matrix_callbacks callbacks,
			 const char *mxid, const char *homeserver, void *userp) {
	struct matrix *matrix = calloc(1, sizeof(*matrix));

	if (!matrix || !(matrix->ll = ll_alloc(free_transfer)) ||
		!(matrix->multi = curl_multi_init()) ||
		(matrix_header_append(matrix, "Content-Type: application/json") ==
		 -1)) {
		matrix_destroy(matrix);

		return NULL;
	}

	{
		size_t len_mxid = strlen(mxid);

		/* We allocate MATRIX_MXID_MAX + 1 bytes. */
		if (len_mxid < 1 || len_mxid > MATRIX_MXID_MAX ||
			(strlen(homeserver)) < 1 ||
			!(matrix->homeserver = strdup(homeserver))) {
			matrix_destroy(matrix);

			return NULL;
		}

		/* Copy NUL aswell, doesn't really matter in our case since we already
		 * zero out everything on allocation. */
		memcpy(matrix->mxid, mxid, len_mxid + 1);
	}

	matrix->cb = callbacks;
	matrix->loop = loop;
	matrix->userp = userp;

	ev_timer_init(&matrix->timer_event, timer_cb, 0., 0.);
	matrix->timer_event.data = matrix;

	curl_multi_setopt(matrix->multi, CURLMOPT_SOCKETFUNCTION, sock_cb);
	curl_multi_setopt(matrix->multi, CURLMOPT_SOCKETDATA, matrix);
	curl_multi_setopt(matrix->multi, CURLMOPT_TIMERFUNCTION, multi_timer_cb);
	curl_multi_setopt(matrix->multi, CURLMOPT_TIMERDATA, matrix);

	return matrix;
}

void
matrix_destroy(struct matrix *matrix) {
	if (!matrix) {
		return;
	}

	/* TODO confirm if it is safe to call this multiple times (If it was already
	 * stopped by a callback. */
	ev_timer_stop(matrix->loop, &matrix->timer_event);

	/* Cleanup the pending easy handles that weren't cleaned up by callbacks. */
	ll_free(matrix->ll);

	curl_multi_cleanup(matrix->multi);
	curl_slist_free_all(matrix->headers);

	free(matrix->homeserver);
	free(matrix);
}
