#include "stb_ds.h"
#include "widgets.h"

#include <assert.h>

uint32_t *
buf_to_uint32_t(const char *buf) {
	assert(buf);

	/* uint32_t has a larger capacity than char so the buffer will always
	 * fit in <= strlen(buf) */
	uint32_t *uint32_buf = NULL;
	arrsetcap(uint32_buf, strlen(buf));

	if (uint32_buf) {
		size_t index = 0;

		while (*buf) {
			assert(index < (arrcap(uint32_buf)));

			int len = tb_utf8_char_to_unicode(&uint32_buf[index], buf);

			if (len == TB_ERR) {
				break;
			}

			index++;
			buf += len;
		}

		arrsetlen(uint32_buf, index);
	}

	return uint32_buf;
}
