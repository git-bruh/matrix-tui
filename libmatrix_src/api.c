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
	assert(endpoint[0] == '/'); /* base[] doesn't have a trailing slash. */

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
		*response = (struct response){
			.easy = easy,
		};

		switch (method) {
		case GET:
			assert(!data);
			return MATRIX_SUCCESS;
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
matrix_sync_forever(struct matrix *matrix, unsigned timeout) {
	if (!matrix->access_token) {
		return MATRIX_NOT_LOGGED_IN;
	}

	char *params = NULL;

	if ((asprintf(&params, "?timeout=%u", timeout)) == -1) {
		return MATRIX_NOMEM;
	}

	enum matrix_code code = MATRIX_CURL_FAILURE;

	char *url = endpoint_create(matrix->homeserver, "/sync", params);

	free(params);

	if (!url) {
		return MATRIX_NOMEM;
	}

	struct curl_slist *headers = get_headers(matrix);
	struct response response = {0};

	size_t new_len = 0;
	char *new_buf = NULL; /* We fill in this buf with the new batch token on
							 every successful response. */

	if ((response_init(GET, NULL, url, headers, &response)) == MATRIX_SUCCESS) {
		for (;;) {
			if (new_buf && (curl_easy_setopt(response.easy, CURLOPT_URL,
											 new_buf)) != CURLE_OK) {
				code = MATRIX_CURL_FAILURE;
				break;
			}

			/* TODO add error callback to allow implementing backoff */
			if ((code = response_perform(&response)) != MATRIX_SUCCESS) {
				break;
			}

			cJSON *parsed = cJSON_Parse(response.data);

			char *next_batch = GETSTR(parsed, "next_batch");

			{
				size_t len_batch = 0;

				if (!next_batch || !(len_batch = strlen(next_batch))) {
					code = MATRIX_MALFORMED_JSON;
					break;
				}

				const char *param_since = "&since=";

				size_t new_len_tmp =
					strlen(url) + +strlen(param_since) + len_batch + 1;

				/* Avoid repeated malloc calls if the token length remains the
				 * same. */
				if (new_len != new_len_tmp) {
					free(new_buf);
					if (!(new_buf = malloc((new_len = new_len_tmp)))) {
						code = MATRIX_NOMEM;
						break;
					}
				}

				snprintf(new_buf, new_len, "%s%s%s", url, param_since,
						 next_batch);
			}

			matrix_dispatch_sync(matrix, parsed);
			cJSON_Delete(parsed);
		}
	}

	curl_slist_free_all(headers);
	response_finish(&response);
	free(url);
	free(new_buf);

	return code;
}

enum matrix_code
matrix_login_with_token(struct matrix *matrix, const char *access_token) {
	if (!access_token) {
		return MATRIX_INVALID_ARGUMENT;
	}

	if ((matrix->access_token = matrix_strdup(access_token))) {
		return MATRIX_SUCCESS;
	}

	return MATRIX_NOMEM;
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

		if ((code = matrix_login_with_token(matrix,
											GETSTR(parsed, "access_token"))) ==
			MATRIX_INVALID_ARGUMENT) {
			code = MATRIX_MALFORMED_JSON; /* token was NULL */
		}

		cJSON_Delete(parsed);
	}

	cJSON_Delete(json);

	response_finish(&response);

	return code;
}
