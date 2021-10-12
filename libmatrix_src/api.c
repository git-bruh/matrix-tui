#include "cJSON.h"
#include "matrix-priv.h"

enum method { GET = 0, POST, PUT };

struct response {
	long http_code;
	size_t len;
	char *data;
	char error[CURL_ERROR_SIZE];
};

static void
response_finish(struct response *response) {
	free(response->data);
}

static bool
http_code_is_success(long code, enum method method) {
	const long success = 200;
	(void) method; /* TODO check if a specific method has a different success
					  code. */
	return code == success;
}

/* TODO get rid of this and just allocate the headers once. */
static struct curl_slist *
get_headers(struct matrix *matrix) {
	assert(matrix->access_token); /* assert as this is only used internally. */

	char *auth = NULL;

	if ((asprintf(&auth, "%s%s", "Authorization: Bearer ",
				  matrix->access_token)) == -1) {
		return NULL;
	}

	struct curl_slist *headers = curl_slist_append(NULL, auth);
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
	params = params ? params : "";

	const char base[] = "/_matrix/client/r0";

	char *final = NULL;

	if ((asprintf(&final, "%s%s%s%s", homeserver, base, endpoint, params)) ==
		-1) {
		return NULL;
	}

	return final;
}

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

static enum matrix_code
perform(struct matrix *matrix, const cJSON *json, enum method method,
		const char endpoint[], const char params[], struct response *response) {
	char *url = endpoint_create(matrix->homeserver, endpoint, params);
	char *data = json ? cJSON_Print(json) : NULL;
	CURL *easy = NULL; /* TODO pool curl handles ? */

	struct curl_slist *headers = get_headers(matrix);

	enum matrix_code code = MATRIX_CURL_FAILURE;

	if (url && (easy = curl_easy_init()) &&
		(curl_easy_setopt(easy, CURLOPT_URL, url)) == CURLE_OK &&
		(curl_easy_setopt(easy, CURLOPT_HTTPHEADER, headers)) == CURLE_OK &&
		(curl_easy_setopt(easy, CURLOPT_ERRORBUFFER, response->error)) ==
			CURLE_OK &&
		(curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, write_cb)) == CURLE_OK &&
		(curl_easy_setopt(easy, CURLOPT_WRITEDATA, response->data)) ==
			CURLE_OK) {
		bool opts_set = false;

		switch (method) {
		case GET:
			assert(!data);
			break;
		case POST:
			assert(data);

			if ((curl_easy_setopt(easy, CURLOPT_POSTFIELDS, data)) ==
				CURLE_OK) {
				opts_set = true;
			}
			break;
		case PUT:
		default:
			assert(0);
		}

		if (opts_set) {
			if ((curl_easy_perform(easy)) == CURLE_OK) {
				curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE,
								  &response->http_code);

				code = http_code_is_success(response->http_code, method)
						   ? MATRIX_SUCCESS
						   : code;
			} else {
				code = MATRIX_CURL_FAILURE;
			}
		}
	}

	curl_easy_cleanup(easy);
	curl_slist_free_all(headers);
	free(data);
	free(url);

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
	cJSON *identifier = NULL;

	struct response response = {0};

	if ((json = cJSON_CreateObject()) &&
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
				 strdup_nullsafe(GETSTR(parsed, "access_token")))) {
			code = MATRIX_SUCCESS;
		}

		cJSON_Delete(parsed);
		response_finish(&response);
	}

	cJSON_Delete(json);

	return code;
}

enum matrix_code
matrix_sync_forever(struct matrix *matrix) {
	if (!matrix->access_token) {
		return MATRIX_NOT_LOGGED_IN;
	}

	return MATRIX_SUCCESS;
}
