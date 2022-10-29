#pragma once
/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <time.h>

enum log_level { LOG_MESSAGE, LOG_WARN, LOG_ERROR, LOG_MAX };

void
log_path_set(void);
const char *
log_path(void);
void
log_mutex_lock(void);
void
log_mutex_unlock(void);
void
log_mutex_destroy(void);

__attribute__((unused)) static inline void
log_level_and_time(enum log_level level) {
	assert(level < LOG_MAX);

	enum { bufsz = 8 + 1 };

	const char level_to_ch[LOG_MAX] = {
	  [LOG_MESSAGE] = 'M',
	  [LOG_WARN] = 'W',
	  [LOG_ERROR] = 'E',
	};

	char buf[bufsz] = {0};
	struct tm tm = {0};

	if ((localtime_r(&(time_t) {time(NULL)}, &tm))) {
		buf[strftime(buf, sizeof(buf), "%I:%M:%S", &tm)] = '\0';
	}

	fprintf(stderr, "[%s] %c: ", buf, level_to_ch[level]);
}

#define LOG(level, ...)                                                        \
	do {                                                                       \
		_Static_assert(level < LOG_MAX, "Invalid log level!");                 \
		log_mutex_lock();                                                      \
		fprintf(stderr, "%s:%d ", __FILE__, __LINE__);                         \
		log_level_and_time(level);                                             \
		fprintf(stderr, __VA_ARGS__);                                          \
		fputc('\n', stderr);                                                   \
		log_mutex_unlock();                                                    \
	} while (0)
