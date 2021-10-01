#include <stddef.h>

int
double_to_int(double x) {
	if (x > INT_MAX) {
		return INT_MAX;
	}

	if (x < INT_MIN) {
		return INT_MIN;
	}

	return x;
}
