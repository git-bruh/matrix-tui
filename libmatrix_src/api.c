#include "cJSON.h"
#include "matrix-priv.h"
#include <assert.h>
#include <stdlib.h>

enum method { GET = 0, POST, PUT };

static char *
endpoint_create(const char *homeserver, const char *endpoint,
				const char *params) {
	params = params ? params : "";

	const char base[] = "/_matrix/client/r0";

	char *final = NULL;

	if ((asprintf(&final, "%s%s%s%s", homeserver, base, endpoint, params)) ==
		-1) {
		return NULL;
	}

	return final;
}

static CURL *
endpoint_create_with_handle(const struct matrix *matrix, const char *endpoint,
							const char *data, const char *params,
							enum method method) {
	char *url = endpoint_create(matrix->homeserver, endpoint, params);
	CURL *easy = NULL;

	if (url && (easy = curl_easy_init()) &&
		(curl_easy_setopt(easy, CURLOPT_URL, url)) == CURLE_OK &&
		(curl_easy_setopt(easy, CURLOPT_HTTPHEADER, matrix->headers)) ==
			CURLE_OK) {
		free(url); /* strdup'd by curl. */
		url = NULL;

		switch (method) {
		case GET:
			assert(!data);

			return easy;
		case POST:
			if ((curl_easy_setopt(easy, CURLOPT_COPYPOSTFIELDS, data)) ==
				CURLE_OK) {
				return easy;
			}
			break;
		case PUT:
			break;
		default:
			assert(0);
		}
	}

	free(url);
	curl_easy_cleanup(easy);

	return NULL;
}

int
matrix_login_with_token(struct matrix *matrix, const char *token) {
	return matrix->authorized ? -1 : matrix_set_authorization(matrix, token);
}

int
matrix_login(struct matrix *matrix, const char *password,
			 const char *device_id) {
	if (matrix->authorized) {
		return -1;
	}

	(void) device_id;

	if (!password || !matrix->cb.login) {
		return -1;
	}

	cJSON *json = NULL;

	if ((json = cJSON_CreateObject()) &&
		(cJSON_AddStringToObject(json, "type", "m.login.password")) &&
		(cJSON_AddStringToObject(json, "password", password))) {
		cJSON *identifier = cJSON_AddObjectToObject(json, "identifier");
		char *rendered = NULL;

		if (identifier &&
			(cJSON_AddStringToObject(identifier, "type", "m.id.user")) &&
			(cJSON_AddStringToObject(identifier, "user", matrix->mxid)) &&
			(rendered = cJSON_PrintUnformatted(json))) {
			CURL *easy = endpoint_create_with_handle(matrix, "/login", rendered,
													 NULL, POST);

			free(rendered);

			if (easy &&
				(matrix_transfer_add(matrix, easy, MATRIX_LOGIN)) == 0) {
				cJSON_Delete(json);

				return 0;
			}

			curl_easy_cleanup(easy);
		}
	}

	cJSON_Delete(json);

	return -1;
}

int
matrix_sync(struct matrix *matrix, int timeout) {
	if (!matrix->authorized) {
		return -1;
	}

	(void) timeout;

	CURL *easy = endpoint_create_with_handle(matrix, "/sync", NULL, "", GET);

	if (easy && (matrix_transfer_add(matrix, easy, MATRIX_SYNC) == 0)) {
		return 0;
	}

	return -1;
}
