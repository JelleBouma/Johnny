#include "johnny_global.h"
#include "johnny_config.h"

struct johnny_file* JOHNNY_FILES;
cmph_t* JOHNNY_HASH;
thread_local long con_bitmap[JOHNNY_CON_BITMAP_LEN] = {0};
thread_local struct connection_context con_ctx[JOHNNY_STACK_CONNECTIONS];