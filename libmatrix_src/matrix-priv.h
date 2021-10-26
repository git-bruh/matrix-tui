#ifndef MATRIX_PRIV_H
#define MATRIX_PRIV_H
#include "cJSON.h"
#include "matrix.h"
#include <assert.h>
#include <curl/curl.h>
#include <math.h>
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

struct matrix {
	char *access_token;
	char *homeserver;
	char *mxid;
	void *userp;
	matrix_sync_cb sync_cb;
};

int
matrix_dispatch_sync(struct matrix *matrix, const cJSON *sync);
int
matrix_double_to_int(double x);
char *
matrix_strdup(const char *s);
#endif /* !MATRIX_PRIV_H */
