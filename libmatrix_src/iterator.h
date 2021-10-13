#pragma once
/* A terrible attempt at making a generic iterator to abstract away the
 * underlying JSON library from the user.
 * TODO investigate a better method:
 *   1. Switch to C++ and pass around `std::unordered_map` objects. (bruh)
 *   2. Expose cJSON objects in the API, will allow more flexiblity in what
 *      can be done with the data but requires the user to be familiar with the
 *      cJSON API.
 *   3. Dump raw JSON as char * (Even more hacky)
 *   4. ??? */

typedef void matrix_iterator_t;

enum matrix_iterator_error {
	MATRIX_ITERATOR_SUCCESS = 0,
	MATRIX_ITERATOR_NOT_FOUND,
	MATRIX_ITERATOR_FINISH,
	MATRIX_ITERATOR_INVALID
};

enum matrix_iterator_type {
	MATRIX_ITERATOR_STRING = 0,
	MATRIX_ITERATOR_INT,
	MATRIX_ITERATOR_UNKNOWN
};

enum matrix_iterator_error
matrix_iterator_next(matrix_iterator_t **iterator,
					 enum matrix_iterator_type type_key,
					 enum matrix_iterator_type type_value, void *key,
					 void *value);

#define MATRIX_TYPENUM(x)                                                      \
	_Generic((x), \
	char **: MATRIX_ITERATOR_STRING, \
	int *: MATRIX_ITERATOR_INT, \
	default: MATRIX_ITERATOR_UNKNOWN)

#define matrix_iterator_next(iterator, key, value)                             \
	matrix_iterator_next(iterator, MATRIX_TYPENUM(key), MATRIX_TYPENUM(value), \
						 key, value)
