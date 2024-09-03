#include "johnny_worker.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/socket.h>

#include "johnny_config.h"
#include "johnny_global.h"

thread_local char stack_data_buffer[JOHNNY_TOTAL_STACK_BUFFER_SIZE];
thread_local static struct epoll_event ev_buf[JOHNNY_EVENTS_BUFFER];
char* http_404 = "HTTP/1.1 404 Not Found\r\nContent-length: 0\r\n\r\n";

hot size_t* get_response_length(connection_context* ctx) {
    return ctx->index == JOHNNY_STACK_CONNECTIONS ? &ctx->extension->response_length : &ctx->response_length;
}

hot char* get_buffer(const connection_context* ctx) {
    return ctx->index == JOHNNY_STACK_CONNECTIONS ? ctx->extension->buffer : stack_data_buffer + ctx->index * JOHNNY_BUFFER_SIZE;
}

hot int_fast32_t johnny_tries_to_allocate_connection_on_stack() {
    int_fast32_t index = 0;
    for (int_fast32_t bitmap_counter = 0; bitmap_counter < JOHNNY_CON_BITMAP_LEN; bitmap_counter++) {
        if (con_bitmap[bitmap_counter] != -1) {
            for (long bits = con_bitmap[bitmap_counter]; bits % 2 == 1; bits >>= 1) {
                index++;
            }
            con_bitmap[bitmap_counter] |= 1 << index;
            break;
        }
        index += JOHNNY_BITS_PER_LONG;
    }
    return index;
}

hot connection_context* johnny_allocates_connection(const int_fast32_t fd) {
    const int_fast32_t index = johnny_tries_to_allocate_connection_on_stack();
    connection_context* new_con;
    if (index == JOHNNY_STACK_CONNECTIONS) {
        new_con = malloc(sizeof(connection_context));
        new_con->extension = malloc(sizeof(heap_connection_context_extension));
        new_con->extension->response_length = 0;
    }
    else {
        new_con = &con_ctx[index];
        new_con->response_length = 0;
    }
    new_con->index = index;
    new_con->rnrnget_slash_counter = 4;
    new_con->buffer_remaining = 0;
    new_con->fd = fd;
    return new_con;
}

hot void johnny_deallocates_connection(connection_context* ctx) {
    if (ctx->index == JOHNNY_STACK_CONNECTIONS) {
        free(ctx->extension);
        free(ctx);
    }
    else
        con_bitmap[ctx->index / JOHNNY_BITS_PER_LONG] ^= 1 << (ctx->index % JOHNNY_BITS_PER_LONG);
}

hot void johnny_closes_connection(connection_context* ctx) {
    close(ctx->fd);
    johnny_deallocates_connection(ctx);
}

hot ssize_t johnny_sends_bytes(connection_context* ctx) {
    size_t* response_length = get_response_length(ctx);
    const ssize_t bytes_sent_cnt = send(ctx->fd, ctx->response, *response_length, MSG_DONTWAIT | MSG_NOSIGNAL);
    if (bytes_sent_cnt == -1) {
        if (errno != EAGAIN && errno != EWOULDBLOCK)
            return -1;
    }
    else {
        ctx->response += bytes_sent_cnt;
        *response_length -= bytes_sent_cnt;
    }
    return 0;
}

hot int johnny_sends_response(connection_context* ctx, const char* file_name) {
    const cmph_uint32 index = cmph_search(JOHNNY_HASH, file_name, strlen(file_name));
    const johnny_file johnny_file = JOHNNY_FILES[index];

    // build response
    const bool found = !strcmp(file_name, johnny_file.url_encoded_file_name);
    ctx->response = found ? johnny_file.response : http_404;
    ctx->response_length = found ? johnny_file.response_length : strlen(ctx->response);

    // send HTTP response to client
    return johnny_sends_bytes(ctx);
}

hot void johnny_handles_requests(connection_context* ctx) {
    ssize_t bytes_received = 1;
    register int rnrnget_slash_counter = ctx->rnrnget_slash_counter;
    register int bytes_parsed = 1;
    char* buffer = get_buffer(ctx);
    while (bytes_received > 0) {
        if (ctx->buffer_remaining != 0) {
            bytes_received = ctx->buffer_remaining;
            ctx->buffer_remaining = 0;
            bytes_parsed = 0;
        }
        else if (bytes_parsed >= bytes_received) {
            const int buffer_offset = rnrnget_slash_counter > 9 ? rnrnget_slash_counter - 9 : 0;
            bytes_received = recv(ctx->fd, buffer + buffer_offset, JOHNNY_BUFFER_SIZE - buffer_offset, MSG_DONTWAIT);
            bytes_parsed = 0;
        }
        for (; bytes_parsed < bytes_received; bytes_parsed++) {
            if (rnrnget_slash_counter >= 9) { // start of file name
                for (; bytes_parsed < bytes_received; bytes_parsed++) {
                    if (buffer[bytes_parsed] == ' ') { // end of file name
                        buffer[bytes_parsed] = '\0';
                        const int succes_code = johnny_sends_response(ctx, buffer + bytes_parsed - rnrnget_slash_counter + 9);
                        if (succes_code == -1)
                            return;
                        if (succes_code == EAGAIN) {
                            memmove(buffer, buffer + bytes_parsed, bytes_received - bytes_parsed);
                            ctx->rnrnget_slash_counter = 0;
                            ctx->buffer_remaining = bytes_received - bytes_parsed;
                            return;
                        }
                        rnrnget_slash_counter = 0;
                        break;
                    }
                    rnrnget_slash_counter++;
                }
                if (rnrnget_slash_counter != 0 && rnrnget_slash_counter < 266) { // found only partial file name
                    if (bytes_parsed != rnrnget_slash_counter + 9)
                        memmove(buffer, buffer + bytes_parsed - rnrnget_slash_counter + 9, rnrnget_slash_counter - 9);
                }
                else
                    rnrnget_slash_counter = 0;
            }
            else if (buffer[bytes_parsed] == "\r\n\r\nGET /"[rnrnget_slash_counter])
                rnrnget_slash_counter++;
            else
                rnrnget_slash_counter = 0;
        }
    }
    if (bytes_received == 0)
        johnny_closes_connection(ctx);
    else
        ctx->rnrnget_slash_counter = rnrnget_slash_counter;
}

hot void johnny_works(const int* server_fd, const int epfd) {
    const int nfds = epoll_wait(epfd, ev_buf, JOHNNY_EVENTS_BUFFER, -1);
    if (nfds < 0)
        perror("calling epoll_wait");
    for (int i = 0; i < nfds; i++) {
        struct epoll_event ev = ev_buf[i];
        if (ev.data.ptr == server_fd) {
            const int fd = accept4(*server_fd, NULL, NULL, SOCK_NONBLOCK);
            if (fd != -1) { // connection made
                connection_context* ctx = johnny_allocates_connection(fd);
                struct epoll_event con_ev = { .data.ptr = ctx, .events = EPOLLIN | EPOLLOUT | EPOLLET};
                epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &con_ev);
            }
        }
        else {
            if (ev.events & EPOLLOUT && *get_response_length(ev.data.ptr) > 0) { // resume sending
                const ssize_t bytes_sent_cnt = johnny_sends_bytes(ev.data.ptr);
                if (bytes_sent_cnt == -1) { // error, cannot send data
                    johnny_closes_connection(ev.data.ptr);
                    return;
                }
            }
            johnny_handles_requests(ev.data.ptr);
        }
    }
}

noreturn void johnny_worker(int server_fd, const int epfd) {
    struct epoll_event ev;
    ev.data.ptr = &server_fd;
    ev.events = EPOLLIN | EPOLLET;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, server_fd, &ev))
        perror("calling epoll_ctl, adding the server fd");
    while (true)
        johnny_works(&server_fd, epfd);
}