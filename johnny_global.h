#ifndef JOHNNY_GLOBAL_H
#define JOHNNY_GLOBAL_H

#include "johnny_config.h"
#include "cmph/src/cmph.h"
#include <threads.h>

#define hot __attribute__ ((hot))

#define JOHNNY_TOTAL_STACK_BUFFER_SIZE (JOHNNY_STACK_CONNECTIONS * JOHNNY_BUFFER_SIZE)
#define JOHNNY_BITS_PER_LONG (sizeof(long) * 8)
#define JOHNNY_CON_BITMAP_LEN (JOHNNY_STACK_CONNECTIONS / JOHNNY_BITS_PER_LONG)

#define JOHNNY_CTX_RDHUP 4

typedef struct johnny_file {
    char* url_encoded_file_name;
    char* response;
    size_t response_length;
} johnny_file;

typedef struct heap_connection_context_extension {
    size_t response_length;
    char buffer[JOHNNY_BUFFER_SIZE];
} heap_connection_context_extension;

typedef struct connection_context {
    short flags;
    short prefix_counter;
    short buffer_offset;
    short buffer_size;
    int index;
    int fd;
    const char* response;
    union {
        size_t response_length;
        heap_connection_context_extension* extension;
    };
} connection_context;

extern johnny_file* JOHNNY_FILES;
extern cmph_t* JOHNNY_HASH;
extern thread_local long con_bitmap[JOHNNY_CON_BITMAP_LEN];
extern thread_local connection_context con_ctx[JOHNNY_STACK_CONNECTIONS];

#endif //JOHNNY_GLOBAL_H
