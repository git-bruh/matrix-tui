#include "iterator.h"
#include "cJSON.h"
#include <math.h>
#include <string.h>

enum matrix_iterator_error
matrix_iterator_next_impl(matrix_iterator_t **iterator,
						  enum matrix_iterator_type type_key,
						  enum matrix_iterator_type type_value, void *key,
						  void *value) {
	if (!*iterator) {
		return MATRIX_ITERATOR_FINISH;
	}

	cJSON *data = *iterator;
	*iterator = data->next;

	/* Only string keys are supported. */
	if (key) {
		if (type_key != MATRIX_ITERATOR_STRING) {
			return MATRIX_ITERATOR_INVALID;
		}

		*(char **) key = data->string;
	}

	if (value) {
		switch (type_value) {
		case MATRIX_ITERATOR_STRING: {
			char *tmp = cJSON_GetStringValue(data);

			if (tmp) {
				*(char **) value = tmp;
				return MATRIX_ITERATOR_SUCCESS;
			}

			return MATRIX_ITERATOR_NOT_FOUND;
		}
		case MATRIX_ITERATOR_INT: {
			double tmp = cJSON_GetNumberValue(data);

			if (!(isnan(tmp))) {
				*(int *) value = double_to_int(tmp);
				return MATRIX_ITERATOR_SUCCESS;
			}

			return MATRIX_ITERATOR_NOT_FOUND;
		}

			return MATRIX_ITERATOR_INVALID;
		}
	}
