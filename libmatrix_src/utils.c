/* SPDX-FileCopyrightText: 2021 git-bruh
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "matrix-priv.h"

int
matrix_double_to_int(double x) {
	assert((!(isnan(x))));

	if (x > INT_MAX) {
		return INT_MAX;
	}

	if (x < INT_MIN) {
		return INT_MIN;
	}

	return (int) x;
}

char *
matrix_strdup(const char *s) {
	return s ? strdup(s) : NULL;
}
