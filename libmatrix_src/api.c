/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "matrix-priv.h"

#include <poll.h>

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

	if (matrix->access_token
		&& (asprintf(
			 &auth, "%s%s", "Authorization: Bearer ", matrix->access_token))
			 == -1) {
		return NULL;
	}

	struct curl_slist *headers = auth ? curl_slist_append(NULL, auth) : NULL;
	struct curl_slist *tmp
	  = curl_slist_append(headers, "Content-Type: application/json");

	if (tmp) {
		free(auth);
		return tmp;
	}

	curl_slist_free_all(headers);
	free(auth);

	return NULL;
}

static char *
endpoint_create(
  const char *homeserver, const char *endpoint, const char *params) {
	assert(homeserver);
	assert(endpoint);
	assert(endpoint[0] == '/'); /* base[] doesn't have a trailing slash. */

	const char base[] = "/_matrix/client/r0";

	char *final = NULL;

	if ((asprintf(&final, "%s%s%s%s", homeserver, base, endpoint,
		  (params ? params : "")))
		== -1) {
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

	if (easy && (curl_easy_setopt(easy, CURLOPT_URL, url)) == CURLE_OK
		&& (curl_easy_setopt(easy, CURLOPT_HTTPHEADER, headers)) == CURLE_OK
		&& (curl_easy_setopt(easy, CURLOPT_ERRORBUFFER, response->error))
			 == CURLE_OK
		&& (curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, write_cb)) == CURLE_OK
		&& (curl_easy_setopt(easy, CURLOPT_WRITEDATA, response)) == CURLE_OK) {
		*response = (struct response) {
		  .easy = easy,
		};

		switch (method) {
		case GET:
			assert(!data);
			return MATRIX_SUCCESS;
		case POST:
			assert(data);

			if ((curl_easy_setopt(easy, CURLOPT_POSTFIELDS, data))
				== CURLE_OK) {
				return MATRIX_SUCCESS;
			}
			break;
		case PUT:
			assert(data);

			if ((curl_easy_setopt(easy, CURLOPT_CUSTOMREQUEST, "PUT"))
				  == CURLE_OK
				&& (curl_easy_setopt(easy, CURLOPT_POSTFIELDS, data))
					 == CURLE_OK) {
				return MATRIX_SUCCESS;
			}

			break;
		default:
			assert(0);
		}
	}

	curl_easy_cleanup(easy);
	memset(response, 0, sizeof(*response));

	return MATRIX_CURL_FAILURE;
}

static enum matrix_code
response_set_code(struct response *response) {
	CURLcode code = curl_easy_getinfo(
	  response->easy, CURLINFO_RESPONSE_CODE, &response->http_code);

	return (code == CURLE_OK && (http_code_is_success(response->http_code)))
		   ? MATRIX_SUCCESS
		   : MATRIX_CURL_FAILURE;
}

static enum matrix_code
response_perform(struct response *response) {
	if ((curl_easy_perform(response->easy)) == CURLE_OK) {
		return response_set_code(response);
	}

	return MATRIX_CURL_FAILURE;
}

static enum matrix_code
response_perform_sync(struct response *response, CURLM *multi,
  struct matrix *matrix, const struct matrix_sync_callbacks *callbacks) {
	int still_running = 0;
	const int timeout_multi
	  = 10; /* 10ms so that we check 'sync_stopped' fast enough. */
	CURLMcode code = CURLM_OK;

	do {
		if (matrix->sync_stopped) {
			break;
		}

		int nfds = 0;
		code = curl_multi_perform(multi, &still_running);

		if (code == CURLM_OK) {
			curl_multi_poll(multi, NULL, 0, timeout_multi, &nfds);
		}
	} while (still_running);

	if (code != CURLM_OK || (response_set_code(response)) != MATRIX_SUCCESS) {
		int backoff
		  = callbacks->backoff_cb ? (callbacks->backoff_cb(matrix)) : -1;

		if (backoff >= 0) {
			poll(NULL, 0, backoff);
		} else {
			return MATRIX_CURL_FAILURE;
		}

		return MATRIX_BACKED_OFF;
	}

	return MATRIX_SUCCESS;
}

static void
response_finish(struct response *response) {
	curl_easy_cleanup(response->easy);
	free(response->data);
}

static unsigned
txn_id(struct matrix *matrix) {
	assert(matrix);

	return ++matrix->txn_id;
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

/* Copy the original url to a new url, adding the batch parameter. */
static enum matrix_code
set_batch(char *url, char **new_url, size_t *new_len, const char *next_batch) {
	if (!next_batch) {
		return MATRIX_MALFORMED_JSON;
	}

	const char *param_since = "&since=";

	size_t new_len_tmp
	  = strlen(url) + strlen(param_since) + strlen(next_batch) + 1;

	/* Avoid repeated malloc calls if the token length remains the
	 * same. We use malloc + snprintf instead of asprintf for this reason. */
	if (*new_len != new_len_tmp) {
		free(*new_url);

		if (!(*new_url = malloc(new_len_tmp))) {
			return MATRIX_NOMEM;
		}

		*new_len = new_len_tmp;
	}

	snprintf(*new_url, *new_len, "%s%s%s", url, param_since, next_batch);

	return MATRIX_SUCCESS;
}

enum matrix_code
matrix_sync_forever(struct matrix *matrix, const char *next_batch,
  unsigned timeout, struct matrix_sync_callbacks callbacks) {
	if (!matrix || !callbacks.sync_cb) {
		return MATRIX_INVALID_ARGUMENT;
	}

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

	CURLM *multi = curl_multi_init();

	if (multi
		&& (next_batch
			  ? ((code = set_batch(url, &new_buf, &new_len, next_batch))
				 == MATRIX_SUCCESS)
			  : true)
		&& (response_init(GET, NULL, url, headers, &response)) == MATRIX_SUCCESS
		&& (curl_multi_add_handle(multi, response.easy)) == CURLM_OK) {
		for (bool backed_off = false;;) {
			if (new_buf
				&& (curl_easy_setopt(response.easy, CURLOPT_URL, new_buf))
					 != CURLE_OK) {
				code = MATRIX_CURL_FAILURE;
				break;
			}

			if ((code
				  = response_perform_sync(&response, multi, matrix, &callbacks))
				!= MATRIX_CURL_FAILURE) {
				/* Restart the transfer. */
				curl_multi_remove_handle(multi, response.easy);
				if ((curl_multi_add_handle(multi, response.easy)) != CURLM_OK) {
					code = MATRIX_CURL_FAILURE;
					break;
				}

				switch (code) {
				case MATRIX_SUCCESS:
					if (backed_off) {
						backed_off = false;

						if (callbacks.backoff_reset_cb) {
							callbacks.backoff_reset_cb(matrix);
						}
					}

					break;
				case MATRIX_BACKED_OFF:
					backed_off = true;
					continue;
				default:
					assert(0);
				}
			} else {
				break;
			}

			cJSON *parsed = cJSON_Parse(response.data);

			if ((code = set_batch(
				   url, &new_buf, &new_len, GETSTR(parsed, "next_batch")))
				!= MATRIX_SUCCESS) {
				cJSON_Delete(parsed);
				break;
			}

			response.len = 0; /* Ensures that we don't realloc extra bytes but
			write the new contents after the old NUL terminator, making us read
			the old data. See "&(response->data[response->len])" in write_cb. */

			matrix_dispatch_sync(matrix, &callbacks, parsed);
			cJSON_Delete(parsed);
		}
	}

	curl_slist_free_all(headers);
	curl_multi_remove_handle(multi, response.easy);
	curl_multi_cleanup(multi);
	response_finish(&response);
	free(url);
	free(new_buf);

	matrix->sync_stopped = false;

	return code;
}

void
matrix_sync_cancel(struct matrix *matrix) {
	matrix->sync_stopped = true;
}

enum matrix_code
matrix_login_with_token(struct matrix *matrix, const char *access_token) {
	if (!matrix || !access_token) {
		return MATRIX_INVALID_ARGUMENT;
	}

	if ((matrix->access_token = matrix_strdup(access_token))) {
		return MATRIX_SUCCESS;
	}

	return MATRIX_NOMEM;
}

enum matrix_code
matrix_login(struct matrix *matrix, const char *password, const char *device_id,
  const char *initial_device_display_name) {
	if (!matrix || !password) {
		return MATRIX_INVALID_ARGUMENT;
	}

	enum matrix_code code
	  = MATRIX_NOMEM; /* Before a call to perform(), only cJSON-related errors
						 are possible. */

	cJSON *json = NULL;
	cJSON *identifier = NULL; /* Free'd when free-ing the above json. */

	struct response response = {0};

	if ((json = cJSON_CreateObject())
		&& (identifier = cJSON_AddObjectToObject(json, "identifier"))
		&& (ADDVARSTR(json, device_id))
		&& (ADDVARSTR(json, initial_device_display_name))
		&& (ADDVARSTR(json, password))
		&& (ADDSTR(json, "type", "m.login.password"))
		&& (ADDSTR(identifier, "type", "m.id.user"))
		&& (ADDSTR(identifier, "user", matrix->mxid))
		&& (code = perform(matrix, json, POST, "/login", NULL, &response))
			 == MATRIX_SUCCESS) {
		cJSON *parsed = cJSON_Parse(response.data);

		if ((code
			  = matrix_login_with_token(matrix, GETSTR(parsed, "access_token")))
			== MATRIX_INVALID_ARGUMENT) {
			code = MATRIX_MALFORMED_JSON; /* token was NULL */
		}

		cJSON_Delete(parsed);
	}

	cJSON_Delete(json);

	response_finish(&response);

	return code;
}

enum matrix_code
matrix_send_message(struct matrix *matrix, char **event_id, const char *room_id,
  const char *msgtype, const char *body, const char *formatted_body) {
	if (!matrix || !event_id || !msgtype || !body) {
		return MATRIX_INVALID_ARGUMENT;
	}

	enum matrix_code code = MATRIX_NOMEM;

	cJSON *json = NULL;
	char *endpoint = NULL;

	const char *format = formatted_body ? "org.matrix.custom.html" : NULL;

	struct response response = {0};

	if ((asprintf(&endpoint, "/rooms/%s/send/%s/%u", room_id, "m.room.message",
		  txn_id(matrix)))
		  != -1
		&& (json = cJSON_CreateObject()) && (ADDVARSTR(json, body))
		&& (ADDVARSTR(json, format)) && (ADDVARSTR(json, formatted_body))
		&& (ADDVARSTR(json, msgtype))
		&& (code = perform(matrix, json, PUT, endpoint, NULL, &response))
			 == MATRIX_SUCCESS) {
		cJSON *parsed = cJSON_Parse(response.data);

		*event_id = matrix_strdup(GETSTR(parsed, "event_id"));

		cJSON_Delete(parsed);
	}

	free(endpoint);
	cJSON_Delete(json);
	response_finish(&response);

	return code;
}
