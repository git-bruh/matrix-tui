#include "fatal.h"
#define STBDS_REALLOC(context, ptr, size) fatal_realloc(ptr, size)
#define STBDS_FREE(context, ptr) free(ptr)
#include "stb/stb_ds.h"
