#ifndef MATRIX_PRIV_H
#define MATRIX_PRIV_H
#include "matrix.h"
#include <assert.h>
#include <curl/curl.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Get the value of a key from an object. */
#define GETSTR(obj, key) (cJSON_GetStringValue(cJSON_GetObjectItem(obj, key)))

struct matrix {
	char *access_token;
	char *homeserver;
	char *mxid;
	void *userp;
	matrix_sync_cb sync_cb;
};

int
matrix_dispatch_sync(const cJSON *sync);
int
matrix_double_to_int(double x);
char *
matrix_strdup(const char *s);
#endif /* !MATRIX_PRIV_H */
