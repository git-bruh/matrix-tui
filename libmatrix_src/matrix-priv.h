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
/* For boolean evaluation */
#define STRSAME(s1, s2) ((strcmp(s1, s2)) == 0)

struct matrix {
	char *access_token;
	char *homeserver;
	char *mxid;
	void *userp;
	struct matrix_callbacks cb;
};

int
matrix_dispatch_sync(const cJSON *sync);
int
matrix_double_to_int(double x);
char *
matrix_strdup(const char *s);
#endif /* !MATRIX_PRIV_H */
