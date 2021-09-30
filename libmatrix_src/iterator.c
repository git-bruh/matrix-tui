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

	cJSON *data = (cJSON *) *iterator;

	/* Only string keys are supported. */
	if (key) {
		if (type_key != MATRIX_ITERATOR_STRING) {
			return MATRIX_ITERATOR_INVALID;
		}

		*(char **) key = data->string;
	}

	if (value) {
		switch (type_value) {
		case MATRIX_ITERATOR_STRING:
			*(char **) value = cJSON_GetStringValue(data);
			break;
		case MATRIX_ITERATOR_UINT: {
			double tmp = cJSON_GetNumberValue(data);
			if ((isnan(tmp))) {
				/* Not a number. */
				*(unsigned *) value = -1;
			} else {
				memcpy(value, &tmp, sizeof(unsigned));
			}
			break;
		}
		default:
			return MATRIX_ITERATOR_INVALID;
		}
	}

	*iterator = data->next;

	return MATRIX_ITERATOR_SUCCESS;
}
