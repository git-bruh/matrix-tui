#include "cJSON.h"
#include "matrix-priv.h"
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>

enum method { GET = 0, POST, PUT };

static char *
endpoint_create(const char *homeserver, const char *endpoint) {
	const char base[] = "/_matrix/client/r0";

	size_t size = strlen(homeserver) + sizeof(base) + strlen(endpoint);

	char *final = calloc(size, sizeof(char));

	if (final) {
		snprintf(final, size, "%s%s%s", homeserver, base, endpoint);
	}

	return final;
}

static CURL *
endpoint_create_with_handle(const char *homeserver, const char *endpoint,
                            const char *data, enum method method) {
	char *url = endpoint_create(homeserver, endpoint);
	CURL *easy = NULL;

	if (url && (easy = curl_easy_init()) &&
	    (curl_easy_setopt(easy, CURLOPT_URL, url)) == CURLE_OK) {

		free(url); /* strdup'd by curl. */
		url = NULL;

		switch (method) {
		case GET:
			break;
		case POST:
			curl_easy_setopt(easy, CURLOPT_COPYPOSTFIELDS, data);
			break;
		case PUT:
			break;
		default:
			assert(0);
		}

		return easy;
	}

	free(url);
	curl_easy_cleanup(easy);

	return NULL;
}

int
matrix_login(struct matrix *matrix, const char *password,
             const char *device_id) {
	(void) device_id;

	if (!password || !matrix->cb.on_login) {
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
			CURL *easy = endpoint_create_with_handle(matrix->homeserver,
			                                         "/login", rendered, POST);

			free(rendered);

			if (easy && (matrix_transfer_add(matrix, easy, false)) == 0) {
				cJSON_Delete(json);

				return 0;
			}

			curl_easy_cleanup(easy);
		}
	}

	if (json) {
		cJSON_Delete(json);
	}

	return -1;
}
