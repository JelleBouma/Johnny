#include <sys/epoll.h>
#include "johnny_global.h"
#include "johnny_config.h"

johnny_file* JOHNNY_FILES;
cmph_t* JOHNNY_HASH;
thread_local long con_bitmap[JOHNNY_CON_BITMAP_LEN] = {0};
thread_local connection_context con_ctx[JOHNNY_STACK_CONNECTIONS];
thread_local struct epoll_event ev_buf[JOHNNY_EVENTS_BUFFER];