#include "matrix-priv.h"

enum method { GET = 0, POST, PUT };

struct response {
	long http_code;
	size_t len;
	char *data;
	CURL *easy;
	char error[CURL_ERROR_SIZE];
};

static size_t
write_cb(void *contents, size_t size, size_t nmemb, void *userp) {
	size_t realsize = size * nmemb;

	struct response *response = userp;

	char *ptr = realloc(response->data, response->len + realsize + 1);

	if (!ptr) {
		return 0;
	}

	response->data = ptr;
	memcpy(&(response->data[response->len]), contents, realsize);
	response->data[response->len += realsize] = '\0';

	return realsize;
}

static bool
http_code_is_success(long code) {
	const long success = 200;
	return code == success;
}

/* TODO get rid of this and just allocate the headers once. */
static struct curl_slist *
get_headers(struct matrix *matrix) {
	char *auth = NULL;

	if (matrix->access_token &&
		(asprintf(&auth, "%s%s", "Authorization: Bearer ",
				  matrix->access_token)) == -1) {
		return NULL;
	}

	struct curl_slist *headers = auth ? curl_slist_append(NULL, auth) : NULL;
	struct curl_slist *tmp =
		curl_slist_append(headers, "Content-Type: application/json");

	if (tmp) {
		free(auth);
		return tmp;
	}

	curl_slist_free_all(headers);
	free(auth);

	return NULL;
}

static char *
endpoint_create(const char *homeserver, const char *endpoint,
				const char *params) {
	assert(homeserver);
	assert(endpoint);

	const char base[] = "/_matrix/client/r0";

	char *final = NULL;

	if ((asprintf(&final, "%s%s%s%s", homeserver, base, endpoint,
				  (params ? params : ""))) == -1) {
		return NULL;
	}

	return final;
}

static enum matrix_code
response_init(enum method method, const char *data, const char *url,
			  const struct curl_slist *headers, struct response *response) {
	assert(headers);
	assert(response);
	assert(url);

	CURL *easy = curl_easy_init();

	if (easy && (curl_easy_setopt(easy, CURLOPT_URL, url)) == CURLE_OK &&
		(curl_easy_setopt(easy, CURLOPT_HTTPHEADER, headers)) == CURLE_OK &&
		(curl_easy_setopt(easy, CURLOPT_ERRORBUFFER, response->error)) ==
			CURLE_OK &&
		(curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, write_cb)) == CURLE_OK &&
		(curl_easy_setopt(easy, CURLOPT_WRITEDATA, response)) == CURLE_OK) {
		response->easy = easy;

		switch (method) {
		case GET:
			assert(!data);
			break;
		case POST:
			assert(data);

			if ((curl_easy_setopt(easy, CURLOPT_POSTFIELDS, data)) ==
				CURLE_OK) {
				return MATRIX_SUCCESS;
			}
			break;
		case PUT:
			break;
		default:
			assert(0);
		}
	}

	curl_easy_cleanup(easy);

	return MATRIX_CURL_FAILURE;
}

static enum matrix_code
response_perform(struct response *response) {
	if ((curl_easy_perform(response->easy)) == CURLE_OK) {
		curl_easy_getinfo(response->easy, CURLINFO_RESPONSE_CODE,
						  &response->http_code);

		if ((http_code_is_success(response->http_code))) {
			return MATRIX_SUCCESS;
		}
	}

	return MATRIX_CURL_FAILURE;
}

static void
response_finish(struct response *response) {
	curl_easy_cleanup(response->easy);
	free(response->data);
}

/* The caller must response_finish() the response. */
static enum matrix_code
perform(struct matrix *matrix, const cJSON *json, enum method method,
		const char endpoint[], const char params[], struct response *response) {
	char *url = endpoint_create(matrix->homeserver, endpoint, params);
	char *data = json ? cJSON_Print(json) : NULL;

	struct curl_slist *headers = get_headers(matrix);

	enum matrix_code code = MATRIX_CURL_FAILURE;

	if (url && headers) {
		code = response_perform(
			((response_init(method, data, url, headers, response)), response));
	}

	curl_slist_free_all(headers);
	free(data);
	free(url);

	return code;
}

enum matrix_code
matrix_sync_forever(struct matrix *matrix, int timeout) {
	const int timeout_min = 1;
	const int timeout_max = 60; // TODO

	if (timeout < timeout_min || timeout > timeout_max) {
		return MATRIX_INVALID_ARGUMENT;
	}

	if (!matrix->access_token) {
		return MATRIX_NOT_LOGGED_IN;
	}

	enum matrix_code code = MATRIX_CURL_FAILURE;

	char *url = endpoint_create(matrix->homeserver, "/sync", NULL);
	struct curl_slist *headers = get_headers(matrix);
	struct response response = {0};

	if ((response_init(GET, NULL, url, headers, &response)) == MATRIX_SUCCESS) {
		for (;;) {
			if ((response_perform(&response)) != MATRIX_SUCCESS) {
				break;
			}

			cJSON *parsed = cJSON_Parse(response.data);
			matrix_dispatch_sync(parsed);
			cJSON_Delete(parsed);
		}
	}

	return code;
}

enum matrix_code
matrix_login(struct matrix *matrix, const char *password,
			 const char *device_id) {
	if (!password) {
		return MATRIX_INVALID_ARGUMENT;
	}

	enum matrix_code code =
		MATRIX_NOMEM; /* Before a call to perform(), only cJSON-related errors
						 are possible. */

	cJSON *json = NULL;
	cJSON *identifier = NULL; /* Free'd when free-ing the above json. */

	struct response response = {0};

	if ((json = cJSON_CreateObject()) &&
		(identifier = cJSON_AddObjectToObject(json, "identifier")) &&
		(cJSON_AddStringToObject(json, "type", "m.login.password")) &&
		(cJSON_AddStringToObject(json, "password", password)) &&
		(cJSON_AddStringToObject(identifier, "type", "m.id.user")) &&
		(cJSON_AddStringToObject(identifier, "user", matrix->mxid)) &&
		(code = perform(matrix, json, POST, "/login", NULL, &response)) ==
			MATRIX_SUCCESS) {
		cJSON *parsed = cJSON_Parse(response.data);

		/* FIXME the code would be MATRIX_NOMEM even if we just received a
		 * malformed JSON. Implement some checking without adding a ton of if
		 * statements. */
		if ((matrix->access_token =
				 matrix_strdup(GETSTR(parsed, "access_token")))) {
			code = MATRIX_SUCCESS;
		}

		cJSON_Delete(parsed);
	}

	cJSON_Delete(json);

	response_finish(&response);

	return code;
}
