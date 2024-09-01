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

hot ssize_t johnny_sends_bytes(connection_context* ctx, const char *response, const size_t response_length) {
    ssize_t bytes_sent_cnt = 0;
    while (bytes_sent_cnt < response_length) {
        //const int flags = response_length - sentBytes > 10240 ? MSG_DONTWAIT | MSG_NOSIGNAL | MSG_ZEROCOPY : MSG_DONTWAIT | MSG_NOSIGNAL;
        const int flags = MSG_DONTWAIT | MSG_NOSIGNAL;
        const ssize_t write_res = send(ctx->fd, response + bytes_sent_cnt, response_length - bytes_sent_cnt, flags);
        if (write_res == -1) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                johnny_closes_connection(ctx);
                return -1;
            }
            break;
        }
        bytes_sent_cnt += write_res;
    }
    return bytes_sent_cnt;
}

hot int johnny_sends_response(connection_context* ctx, const char* file_name, const int epfd) {
    const cmph_uint32 index = cmph_search(JOHNNY_HASH, file_name, strlen(file_name));
    const johnny_file johnny_file = JOHNNY_FILES[index];

    // build response
    const bool found = !strcmp(file_name, johnny_file.url_encoded_file_name);
    const char* response = found ? johnny_file.response : http_404;
    const size_t response_length = found ? johnny_file.response_length : strlen(response);

    // send HTTP response to client
    const size_t bytes_sent_cnt = johnny_sends_bytes(ctx, response, response_length);
    if (bytes_sent_cnt == -1)
        return -1;
    if (bytes_sent_cnt < response_length) {
        ctx->response = response + bytes_sent_cnt;
        *get_response_length(ctx) = response_length - bytes_sent_cnt;
        struct epoll_event ev = { .data.ptr = ctx, .events = EPOLLOUT | EPOLLET };
        epoll_ctl(epfd, EPOLL_CTL_MOD, ctx->fd, &ev);
        return EAGAIN;
    }
    return 0;
}

hot void johnny_handles_requests(connection_context* ctx, const int epfd) {
    //float startTime = (float)clock()/CLOCKS_PER_SEC;
    size_t* response_length = get_response_length(ctx);
    if (*response_length > 0) {
        const ssize_t bytes_sent_cnt = johnny_sends_bytes(ctx, ctx->response, ctx->response_length);
        if (bytes_sent_cnt == -1)
            return;
        *response_length -= bytes_sent_cnt;
        if (*response_length == 0) {
            struct epoll_event ev = { .data.ptr = ctx, .events = EPOLLIN | EPOLLET };
            epoll_ctl(epfd, EPOLL_CTL_MOD, ctx->fd, &ev);
        }
    }
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
                        //printf("johnny_handles_requests time to start responding in %f\r\n", (float)clock()/CLOCKS_PER_SEC - startTime); // 1 - 57 us
                        const int succes_code = johnny_sends_response(ctx, buffer + bytes_parsed - rnrnget_slash_counter + 9, epfd);
                        if (succes_code == -1)
                            return;
                        if (succes_code == EAGAIN) {
                            memcpy(buffer, buffer + bytes_parsed, bytes_received - bytes_parsed);
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
                        memcpy(buffer, buffer + bytes_parsed - rnrnget_slash_counter + 9, rnrnget_slash_counter - 9);
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
        //float start_time = (float)clock()/CLOCKS_PER_SEC;
        if (ev_buf[i].data.ptr == server_fd) {
            const int fd = accept4(*server_fd, NULL, NULL, SOCK_NONBLOCK);
            if (fd != -1) { // connection made
                // int yes = 1;
                // if (setsockopt(fd, SOL_SOCKET, SO_ZEROCOPY, &yes, sizeof(yes)))
                //     perror("calling setsockopt zerocopy");
                //
                // timer_ctx[connections].fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
                //
                // timerfd_settime(timer_ctx[connections].fd, 0, &timer_spec, NULL);
                // timer_ctx[connections].handler = johnny_closes_connection;
                //
                // timer_ctx[connections].joint_context = &con_ctx[connections];
                // con_ctx[connections].joint_context = &timer_ctx[connections];
                //
                // timer_ev[connections].data.ptr = &timer_ctx[connections];
                // timer_ev[connections].events = EPOLLIN | EPOLLET;
                // epoll_ctl(epfd, EPOLL_CTL_ADD, timer_ctx[connections].fd, &timer_ev[connections]);
                connection_context* con = johnny_allocates_connection(fd);
                struct epoll_event con_ev = { .data.ptr = con, .events = EPOLLIN | EPOLLET};
                epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &con_ev);
            }
            //printf("johnny_listen fd %i time to handle in %f\r\n", fd, (float)clock()/CLOCKS_PER_SEC - start_time); // listen 5 - 29 us
        }
        else {
            johnny_handles_requests(ev_buf[i].data.ptr, epfd);
            //printf("johnny_worker fd %i time to handle in %f\r\n", ctx->fd, (float)clock()/CLOCKS_PER_SEC - startTime); // request 31 - 1127 us, timer 60 - 262 us
        }
    }
}

noreturn void johnny_worker(int server_fd, const int epfd) {
    struct itimerspec timer_spec = { .it_interval = {.tv_sec = JOHNNY_INACTIVE_CONNECTION_TIMEOUT, .tv_nsec = 0}, .it_value = {.tv_sec = 5, .tv_nsec = JOHNNY_INACTIVE_CONNECTION_TIMEOUT}};
    struct epoll_event ev;
    ev.data.ptr = &server_fd;
    ev.events = EPOLLIN | EPOLLET;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, server_fd, &ev))
        perror("calling epoll_ctl");
    while (true)
        johnny_works(&server_fd, epfd);
}