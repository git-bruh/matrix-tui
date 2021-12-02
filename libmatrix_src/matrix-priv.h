/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef MATRIX_PRIV_H
#define MATRIX_PRIV_H
#include "matrix.h"

#include <assert.h>
#include <cjson/cJSON.h>
#include <curl/curl.h>
#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

/* Get the value of a key from an object. */
#define GETSTR(obj, key) (cJSON_GetStringValue(cJSON_GetObjectItem(obj, key)))
/* Set a key with a value if val is non-NULL and check whether the operation was
 * successful. */
/* const char cast to make clang-tidy shut up about evaluating string literal as
 * bool. */
#define ADDSTR(obj, key, val)                                                  \
	(!!((const char *) (val)) ? (!!(cJSON_AddStringToObject(obj, key, val)))   \
							  : true)
/* Stringify the variable name and set it's value as the key. */
#define ADDVARSTR(obj, var) (ADDSTR(obj, #var, var))

struct node {
	void *data;
	struct node *next;
	struct node *prev;
};

struct ll {
	struct node *tail;
	void (*free)(void *data);
};

struct matrix {
	_Atomic bool cancelled;
	_Atomic unsigned txn_id;
	struct ll *transfers;
	char *access_token;
	char *homeserver;
	char *mxid;
	void *userp;
};

struct ll *
matrix_ll_alloc(void (*free)(void *data));
struct node *
matrix_ll_append(struct ll *ll, void *data);
void
matrix_ll_remove(struct ll *ll, struct node *node);
void
matrix_ll_free(struct ll *ll);

int
matrix_dispatch_sync(struct matrix *matrix,
  const struct matrix_sync_callbacks *callbacks, const cJSON *sync);
int
matrix_double_to_int(double x);
char *
matrix_strdup(const char *s);
#endif /* !MATRIX_PRIV_H */
