/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "stb_ds.h"
#include "ui.h"
#include "widgets.h"

#include <assert.h>
#include <math.h>

static double
str_to_hue(const unsigned char *str) {
	assert(str);

	const unsigned long hash_initial = 5381;
	const unsigned long shift = 5;

	unsigned long hash = hash_initial;
	int c = 0;

	while ((c = *str++)) {
		hash = ((hash << shift) + hash) + (unsigned long) c; /* hash * 33 + c */
	}

	return (double) (hash % HUE_MAX);
}

/* This function has a bunch of random variable names and magic numbers that I
 * am too dumb to study about and label appropriately. I just translated it
 * from JS to C, original source: https://stackoverflow.com/a/64090995 */
uintattr_t
hsl_to_rgb(double h, double s, double l) {
	const double scale_down = 100.0;

	assert(h <= HUE_MAX);
	assert(s <= scale_down);
	assert(l <= scale_down);

	/* Must be between 0.0 and 1.0 */
	s /= scale_down;
	l /= scale_down;

	enum { rgb = 3, rgb_max = 255 };

	const double vals[rgb] = {0.0, 8.0, 4.0};

#ifndef TB_OPT_TRUECOLOR
	const double mults[rgb] = {7.0, 7.0, 3.0};
	const int lshifts[rgb] = {5, 2, 0};
#else
	const int lshifts[rgb] = {16, 8, 0};
#endif

	double a = s * fmin(l, 1.0 - l);

	uintattr_t out = 0;

	for (size_t i = 0; i < rgb; i++) {
		/* NOLINTNEXTLINE */
		double k = fmod(vals[i] + (h / 30.0), 12.0);
		/* NOLINTNEXTLINE */
		double result = l - (a * fmax(fmin(fmin(k - 3.0, 9.0 - k), 1.0), -1.0));

		assert(result >= 0.0);
		assert(result <= 1.0);

#ifndef TB_OPT_TRUECOLOR
		/* https://stackoverflow.com/a/37874124 */
		int tmp = (int) round(result * mults[i]) << lshifts[i];
		assert(((int) out + tmp) <= rgb_max);

		out += (uintattr_t) tmp;
#else
		/* https://haacked.com/archive/2009/12/29/convert-rgb-to-hex.aspx */
		out |= ((uintattr_t) round(result * 255.0) << lshifts[i]); /* NOLINT */
#endif
	}

	return out;
}

uint32_t *
buf_to_uint32_t(const char *buf, size_t len) {
	assert(buf);

	/* uint32_t has a larger capacity than char so the buffer will always
	 * fit in <= strlen(buf) */
	uint32_t *uint32_buf = NULL;

	if (len == 0) {
		len = strlen(buf);
	}

	arrsetcap(uint32_buf, len);

	if (uint32_buf) {
		size_t index = 0;

		for (size_t i = 0; i < len; index++) {
			assert(i < (arrcap(uint32_buf)));

			int len_ch = tb_utf8_char_to_unicode(&uint32_buf[index], &buf[i]);

			if (len_ch == TB_ERR) {
				break;
			}

			i += (size_t) len_ch;
		}

		arrsetlen(uint32_buf, index);
	}

	return uint32_buf;
}

uint32_t *
mxid_to_uint32_t(const char *mxid) {
	assert(mxid);

	enum {
		start_index = 1,
		min_len = 4, /* @x:y */
	};

	if ((strnlen(mxid, min_len)) != min_len) {
		return NULL;
	}

	char *end_colon = strchr(mxid, ':');

	if (!end_colon) {
		return NULL;
	}

	return buf_to_uint32_t(&mxid[start_index], (size_t) (end_colon - mxid));
}

uintattr_t
str_attr(const char *str) {
	assert(str);

	return hsl_to_rgb(
	  str_to_hue((const unsigned char *) str), SATURATION, LIGHTNESS);
}
