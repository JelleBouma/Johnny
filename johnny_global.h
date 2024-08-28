#ifndef JOHNNY_GLOBAL_H
#define JOHNNY_GLOBAL_H

#include "johnny_config.h"
#include "cmph/src/cmph.h"
#include <threads.h>

#define hot __attribute__ ((hot))

#define JOHNNY_BITS_PER_LONG (sizeof(long) * 8)
#define JOHNNY_CON_BITMAP_LEN (JOHNNY_STACK_CONNECTIONS / JOHNNY_BITS_PER_LONG)

typedef struct johnny_file {
    char* url_encoded_file_name;
    char* response;
    size_t response_length;
} johnny_file;

typedef struct connection_context {
    char buffer[JOHNNY_BUFFER_SIZE];
    int fd;
    int index;
    int rnrnget_slash_counter;
    const char* response;
    size_t response_length;
    int buffer_remaining;
} connection_context;

extern johnny_file* JOHNNY_FILES;
extern cmph_t* JOHNNY_HASH;
extern thread_local long con_bitmap[JOHNNY_CON_BITMAP_LEN];
extern thread_local connection_context con_ctx[JOHNNY_STACK_CONNECTIONS];
extern thread_local struct epoll_event ev_buf[JOHNNY_EVENTS_BUFFER];

#endif //JOHNNY_GLOBAL_H
