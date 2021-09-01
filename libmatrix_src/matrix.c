/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: LGPL-3.0-or-later */

#include "matrix.h"
#include <stdlib.h>
#include <stdbool.h>

struct matrix {
	struct matrix_callbacks callbacks;
	struct ev_loop *loop;
	struct ev_timer timer_event;
	CURLM *multi;
	int still_running;
};

/* Curl callbacks adapted from https://curl.se/libcurl/c/evhiperfifo.html. */
struct sock_info {
    struct ev_io ev;
    CURL *easy;
    curl_socket_t sockfd;
    int action;
    bool evset;
    long timeout;
};

static void check_multi_info(struct matrix *matrix) {
    CURLMsg *msg = NULL;
    int msgs_left = 0;

    while ((msg = curl_multi_info_read(matrix->multi, &msgs_left))) {
        if (msg->msg == CURLMSG_DONE) {
            curl_multi_remove_handle(matrix->multi, msg->easy_handle);
            curl_easy_cleanup(msg->easy_handle);
        }
    }
}

static void event_cb(EV_P_ struct ev_io *w, int revents) {
    struct matrix *matrix = (struct matrix *)w->data;

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

static void timer_cb(EV_P_ struct ev_timer *w, int revents) {
    (void)revents;

    struct matrix *matrix = (struct matrix *)w->data;

    if ((curl_multi_socket_action(matrix->multi, CURL_SOCKET_TIMEOUT, 0,
                                  &matrix->still_running)) == CURLM_OK) {
        check_multi_info(matrix);
    }
}

static int multi_timer_cb(CURLM *multi, long timeout_ms,
                          struct matrix *matrix) {
    (void)multi;

    ev_timer_stop(matrix->loop, &matrix->timer_event);

    /* -1 indicates that we should stop the timer. */
    if (timeout_ms >= 0) {
        double seconds = (double)(timeout_ms / 1000);

        ev_timer_init(&matrix->timer_event, timer_cb, seconds, 0.);
        ev_timer_start(matrix->loop, &matrix->timer_event);
    }

    return 0;
}

static void remsock(struct sock_info *sock_info, struct matrix *matrix) {
    if (sock_info) {
        if (sock_info->evset) {
            ev_io_stop(matrix->loop, &sock_info->ev);
        }

        free(sock_info);
    }
}

static void setsock(struct sock_info *sock_info, curl_socket_t sockfd,
                    CURL *easy, int action, struct matrix *matrix) {
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

static void addsock(curl_socket_t sockfd, CURL *easy, int action,
                    struct matrix *matrix) {
    struct sock_info *sock_info = calloc(sizeof(*sock_info), 1);

    setsock(sock_info, sockfd, easy, action, matrix);

    curl_multi_assign(matrix->multi, sockfd, sock_info); 
}

static int sock_cb(CURL *easy, curl_socket_t sockfd, int what, void *userp,
                   void *sockp) {
    struct matrix *matrix = (struct matrix *)userp;
    struct sock_info *sock_info = (struct sock_info *)sockp;

    if (what == CURL_POLL_REMOVE) {
        remsock(sock_info, matrix);
    } else {
        if (!sock_info) {
            addsock(sockfd, easy, what, matrix);
        } else {
            setsock(sock_info, sockfd, easy, what, matrix);
        }
    }

    return 0;
}

struct matrix *matrix_alloc(struct ev_loop *loop) {
	struct matrix *matrix = calloc(1, sizeof(*matrix));

	if (!matrix) {
		return NULL;
	}

	matrix->multi = curl_multi_init();

	if (!matrix->multi) {
		free(matrix);

		return NULL;
	}

	matrix->loop = loop;

	ev_timer_init(&matrix->timer_event, timer_cb, 0., 0.);
	matrix->timer_event.data = matrix;

	curl_multi_setopt(matrix->multi, CURLMOPT_SOCKETFUNCTION, sock_cb);
	curl_multi_setopt(matrix->multi, CURLMOPT_SOCKETDATA, matrix);
	curl_multi_setopt(matrix->multi, CURLMOPT_TIMERFUNCTION, multi_timer_cb);
	curl_multi_setopt(matrix->multi, CURLMOPT_TIMERDATA, matrix);

	return matrix;
}

void matrix_destroy(struct matrix *matrix) {
	if (!matrix) {
		return;
	}

	/* TODO cleanup pending easy handles that weren't cleaned up by callbacks. */
	curl_multi_cleanup(matrix->multi);

	free(matrix);
}

int matrix_begin_sync(struct matrix *matrix, int timeout) {
	CURL *easy = curl_easy_init();

	if (!easy) {
		return -1;
	}

	/* curl_easy_setopt(easy, CURLOPT_URL, "https://duckduckgo.com"); */

	if ((curl_multi_add_handle(matrix->multi, easy)) != CURLM_OK) {
		curl_easy_cleanup(easy);

		return -1;
	}

	return 0;
}
