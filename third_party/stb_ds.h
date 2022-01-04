#include "stb/stb_ds.h"

/* https://github.com/nothings/stb/issues/1271 */
#define stbds_shgeti_ts(t, k, temp)                                            \
	((assert(t)),                                                              \
	  stbds_hmget_key_ts_wrapper((t), sizeof *(t), (void *) (k),               \
		sizeof(t)->key, &(temp), STBDS_HM_STRING),                             \
	  (temp))

#define stbds_shgetp_ts(t, k, temp)                                            \
	((void) stbds_shgeti_ts(t, k, temp), &(t)[temp])

#define stbds_shget_ts(t, k, temp) (stbds_shgetp_ts(t, k, temp)->value)

#ifndef STBDS_NO_SHORT_NAMES
#define shget_ts stbds_shget_ts
#define shgeti_ts stbds_shgeti_ts
#define shgetp_ts stbds_shgetp_ts
#endif
