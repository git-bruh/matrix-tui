#pragma once
/* A terrible attempt at making a generic iterator to abstract away the
 * underlying JSON library from the user. */

typedef void matrix_iterator_t;

enum matrix_iterator_error {
	MATRIX_ITERATOR_SUCCESS = 0,
	MATRIX_ITERATOR_FINISH,
	MATRIX_ITERATOR_INVALID
};

enum matrix_iterator_type {
	MATRIX_ITERATOR_STRING = 0,
	MATRIX_ITERATOR_UINT,
	MATRIX_ITERATOR_ITERATOR,
	MATRIX_ITERATOR_UNKNOWN
};

/* Must be called via the matrix_iterator_next() macro. */
enum matrix_iterator_error
matrix_iterator_next_impl(
	matrix_iterator_t **iterator, // struct matrix_iterator *iterator,
	enum matrix_iterator_type type_key, enum matrix_iterator_type type_value,
	void *key, void *value);

#define MATRIX_TYPENUM(x)                                                      \
	_Generic((x), \
	char **: MATRIX_ITERATOR_STRING, \
	unsigned *: MATRIX_ITERATOR_UINT, \
	struct matrix_iterator *: MATRIX_ITERATOR_ITERATOR, \
	default: MATRIX_ITERATOR_UNKNOWN)

#define matrix_iterator_next(iterator, key, value)                             \
	matrix_iterator_next_impl(iterator, MATRIX_TYPENUM(key),                   \
							  MATRIX_TYPENUM(value), key, value)
