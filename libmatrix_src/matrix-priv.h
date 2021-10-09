#pragma once
#include "matrix.h"
#include <curl/curl.h>
#include <stddef.h>
#include <string.h>

/* Get the value of a key from an object. */
#define GETSTR(obj, key) (cJSON_GetStringValue(cJSON_GetObjectItem(obj, key)))

struct matrix {
	struct matrix_callbacks cb;
	char *access_token;
	char *homeserver;
	char *mxid;
	void *userp;
};

/* FIXME is there a way to move these functions into a .c file without prefixing
 * them with "matrix_" while keeping them hidden from library users ? */
static int
double_to_int(double x) {
	if (x > INT_MAX) {
		return INT_MAX;
	}

	if (x < INT_MIN) {
		return INT_MIN;
	}

	return x;
}

static char *
strdup_nullsafe(const char *s) {
	return s ? strdup(s) : NULL;
}
