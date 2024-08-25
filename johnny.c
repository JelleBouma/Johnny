#define _GNU_SOURCE
#include <arpa/inet.h>
#include <dirent.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include "file_io.h"
#include "cmph/src/cmph.h"

#define JOHNNY_BUFFER_SIZE 1024
#define JOHNNY_STACK_CONNECTIONS 1024
#define JOHNNY_EVENTS_BUFFER 1024
#define JOHNNY_PORT 5001
#define JOHNNY_INACTIVE_CONNECTION_TIMEOUT 30
#define JOHNNY_ROOT "/mnt/c/Users/JelleBouma/pictures/"

#define JOHNNY_BITS_PER_LONG (sizeof(long) * 8)
#define JOHNNY_CON_BITMAP_LEN (JOHNNY_STACK_CONNECTIONS / JOHNNY_BITS_PER_LONG)

struct johnny_file {
    char* url_encoded_file_name;
    char* response;
    size_t response_length;
};

struct connection_context {
    char buffer[JOHNNY_BUFFER_SIZE];
    int fd;
    int index;
    int rnrnget_slash_counter;
};

struct johnny_file* JOHNNY_FILES;
cmph_t* JOHNNY_HASH;
thread_local long con_bitmap[JOHNNY_CON_BITMAP_LEN] = {0};
thread_local struct connection_context con_ctx[JOHNNY_STACK_CONNECTIONS];

char* get_file_extension(const char *file_name) {
    char* dot = strrchr(file_name, '.');
    if (!dot || dot == file_name) {
        return "";
    }
    return dot + 1;
}

char* url_encode(const char* originalText)
{
    // allocate memory for the worst possible case (all characters need to be encoded)
    char* encodedText = malloc(sizeof(char)*strlen(originalText)*3+1);

    const char* hex = "0123456789abcdef";

    int pos = 0;
    for (int i = 0; i < strlen(originalText); i++) {
        if (('a' <= originalText[i] && originalText[i] <= 'z')
            || ('A' <= originalText[i] && originalText[i] <= 'Z')
            || ('0' <= originalText[i] && originalText[i] <= '9')
            || (originalText[i] == '-')
            || (originalText[i] == '_')
            || (originalText[i] == '~')
            || (originalText[i] == '.')
            || (originalText[i] == '/')) {
            encodedText[pos++] = originalText[i];
            } else {
                encodedText[pos++] = '%';
                encodedText[pos++] = hex[originalText[i] >> 4];
                encodedText[pos++] = hex[originalText[i] & 15];
            }
    }
    encodedText[pos] = '\0';
    return encodedText;
}

int count_files(char* dir_name) {
    printf("counting files in directory %s\n", dir_name);
    int file_count = 0;
    DIR* d;
    struct dirent* dir_ent;
    d = opendir(dir_name);
    if (d) {
        while ((dir_ent = readdir(d)) != NULL)
            if (strcmp(dir_ent->d_name, ".") && strcmp(dir_ent->d_name, "..")) {
                char path[strlen(dir_name) + strlen(dir_ent->d_name) + 2];
                strcpy(path, dir_name);
                strcat(path, dir_ent->d_name);
                struct stat sb;
                lstat(path, &sb);
                if ((sb.st_mode & S_IFMT) == S_IFREG)
                    file_count++;
                else if ((sb.st_mode & S_IFMT) == S_IFDIR) {
                    strcat(path, "/");
                    file_count += count_files(path);
                }
            }
        closedir(d);
    }
    return file_count;
}

const char* get_mime_type(const char *file_ext) {
    if (!strcasecmp(file_ext, "jpg") || !strcasecmp(file_ext, "jpeg"))
        return "image/jpeg";
    if (!strcasecmp(file_ext, "png"))
        return "image/png";
    if (!strcasecmp(file_ext, "apng"))
        return "image/apng";
    if (!strcasecmp(file_ext, "avif"))
        return "image/avif";
    if (!strcasecmp(file_ext, "gif"))
        return "image/gif";

    if (!strcasecmp(file_ext, "html"))
        return "text/html";
    if (!strcasecmp(file_ext, "js"))
        return "text/javascript";
    if (!strcasecmp(file_ext, "css"))
        return "text/css";

    if (!strcasecmp(file_ext, "otf"))
        return "font/otf";
    if (!strcasecmp(file_ext, "ttf"))
        return "font/ttf";
    if (!strcasecmp(file_ext, "woff"))
        return "font/woff";
    if (!strcasecmp(file_ext, "woff2"))
        return "font/woff2";

    if (!strcasecmp(file_ext, "json"))
        return "application/json";
    if (!strcasecmp(file_ext, "wasm"))
        return "application/wasm";
    return "application/octet-stream";
}

const char* get_content_encoding(const char *file_ext) {
    if (!strcasecmp(file_ext, "gz"))
        return "gzip";
    if (!strcasecmp(file_ext, "br"))
        return "br";
    if (!strcasecmp(file_ext, "zstd"))
        return "zstd";

    return NULL;
}

int_fast32_t johnny_tries_to_allocate_connection_on_stack() {
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

struct connection_context* johnny_allocates_connection(int_fast32_t fd) {
    int_fast32_t index = johnny_tries_to_allocate_connection_on_stack();
    struct connection_context* new_con = index == JOHNNY_STACK_CONNECTIONS ? malloc(sizeof(struct connection_context)) : &con_ctx[index];
    new_con->index = index;
    new_con->rnrnget_slash_counter = 4;
    new_con->fd = fd;
    return new_con;
}

void johnny_deallocates_connection(struct connection_context* ctx) {
    if (ctx->index == JOHNNY_STACK_CONNECTIONS)
        free(ctx);
    else
        con_bitmap[ctx->index / JOHNNY_BITS_PER_LONG] ^= 1 << (ctx->index % JOHNNY_BITS_PER_LONG);
}

void johnny_closes_connection(struct connection_context* ctx) {
    close(ctx->fd);
    johnny_deallocates_connection(ctx);
}

int johnny_sends_response(int client_fd, char* file_name) {
    cmph_uint32 index = cmph_search(JOHNNY_HASH, file_name, strlen(file_name));
    struct johnny_file johnny_file = JOHNNY_FILES[index];

    // build response
    const bool found = !strcmp(file_name, johnny_file.url_encoded_file_name);
    const char* response = found ? johnny_file.response : "HTTP/1.1 404 Not Found\r\nContent-length: 0\r\n\r\n";
    const size_t response_length = found ? johnny_file.response_length : strlen(response);

    // send HTTP response to client
    size_t sentBytes = 0;
    while (sentBytes < response_length) {
        int flags = response_length > 10240 ? MSG_DONTWAIT | MSG_NOSIGNAL | MSG_ZEROCOPY : MSG_DONTWAIT | MSG_NOSIGNAL;
        int writeRes = send(client_fd, response + sentBytes, response_length - sentBytes, flags);
        if (writeRes <= 0) {
            perror("calling send");
            return -1;
        }
        sentBytes += writeRes;
    }
    return 0;
}

void johnny_handles_requests(struct connection_context* ctx) {
    //float startTime = (float)clock()/CLOCKS_PER_SEC;
    ssize_t bytes_received = 1;
    int bytes_parsed = 1;
    while (bytes_received > 0) {
        if (bytes_parsed >= bytes_received) {
            int buffer_offset = ctx->rnrnget_slash_counter > 9 ? ctx->rnrnget_slash_counter - 9 : 0;
            bytes_received = recv(ctx->fd, ctx->buffer + buffer_offset, 1024 - buffer_offset, MSG_DONTWAIT);
            bytes_parsed = 0;
        }
        for (; bytes_parsed < bytes_received; bytes_parsed++) {
            if (ctx->rnrnget_slash_counter >= 9) { // start of file name
                for (; bytes_parsed < bytes_received; bytes_parsed++) {
                    if (ctx->buffer[bytes_parsed] == ' ') { // end of file name
                        ctx->buffer[bytes_parsed] = '\0';
                        //printf("johnny_handles_requests time to start responding in %f\r\n", (float)clock()/CLOCKS_PER_SEC - startTime); // 1 - 57 us
                        if (johnny_sends_response(ctx->fd, ctx->buffer + bytes_parsed - ctx->rnrnget_slash_counter + 9)) {
                            perror("calling johnny_sends_response");
                            close(ctx->fd);
                            return;
                        }
                        ctx->rnrnget_slash_counter = 0;
                        break;
                    }
                    ctx->rnrnget_slash_counter++;
                }
                if (ctx->rnrnget_slash_counter != 0 && ctx->rnrnget_slash_counter < 266) { // found only partial file name
                    if (bytes_parsed != ctx->rnrnget_slash_counter + 9)
                        memcpy(ctx->buffer, ctx->buffer + bytes_parsed - ctx->rnrnget_slash_counter + 9, ctx->rnrnget_slash_counter - 9);
                }
                else
                    ctx->rnrnget_slash_counter = 0;
            }
            else if (ctx->buffer[bytes_parsed] == "\r\n\r\nGET /"[ctx->rnrnget_slash_counter])
                ctx->rnrnget_slash_counter++;
            else
                ctx->rnrnget_slash_counter = 0;
        }
    }
    if (bytes_received == 0)
        johnny_closes_connection(ctx);
}

int johnny_worker_setup() {
    // create server socket
    int server_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (server_fd < 0) {
        perror("calling socket");
        exit(EXIT_FAILURE);
    }

    // config socket
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(JOHNNY_PORT);
    memset(server_addr.sin_zero, 0, 8);

    // lose the pesky "Address already in use" error message
    int yes = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes) == -1) {
        perror("calling setsockopt SO_REUSEADDR");
        exit(1);
    }

    // bind multiple sockets to a single port
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof yes) == -1) {
        perror("calling setsockopt SO_REUSEPORT");
        exit(1);
    }

    // bind socket to port
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("calling bind");
        exit(EXIT_FAILURE);
    }

    // listen for connections
    if (listen(server_fd, 1024) < 0) {
        perror("calling listen");
        exit(EXIT_FAILURE);
    }

    printf("Johnny listening on port %d\n", JOHNNY_PORT);

    return server_fd;
}

void johnny_worker() {
    int server_fd = johnny_worker_setup();
    struct epoll_event ev, evs[JOHNNY_EVENTS_BUFFER];
    struct itimerspec timer_spec = { .it_interval = {.tv_sec = JOHNNY_INACTIVE_CONNECTION_TIMEOUT, .tv_nsec = 0}, .it_value = {.tv_sec = 5, .tv_nsec = JOHNNY_INACTIVE_CONNECTION_TIMEOUT}};
    int epfd = epoll_create1(0);
    if (epfd == -1)
        perror("calling epoll_create1");
    ev.data.ptr = &server_fd;
    ev.events = EPOLLIN | EPOLLET;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, server_fd, &ev))
        perror("calling epoll_ctl");
    while(true) {
        int nfds = epoll_wait(epfd, evs, JOHNNY_EVENTS_BUFFER, -1);
        if (nfds < 0)
            perror("calling epoll_wait");
        for (int i = 0; i < nfds; i++) {
            //float start_time = (float)clock()/CLOCKS_PER_SEC;
            if (evs[i].data.ptr == &server_fd) {
                int fd = accept4(server_fd, NULL, NULL, SOCK_NONBLOCK);
                if (fd != -1) { // connection made
                    int yes = 1;
                    if (setsockopt(fd, SOL_SOCKET, SO_ZEROCOPY, &yes, sizeof(yes)))
                        perror("calling setsockopt zerocopy");
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
                    struct connection_context* con = johnny_allocates_connection(fd);
                    struct epoll_event con_ev = { .data.ptr = con, .events = EPOLLIN | EPOLLET};
                    epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &con_ev);
                }
                //printf("johnny_listen fd %i time to handle in %f\r\n", fd, (float)clock()/CLOCKS_PER_SEC - start_time); // listen 5 - 29 us
            }
            else {
                johnny_handles_requests(evs[i].data.ptr);
                //printf("johnny_worker fd %i time to handle in %f\r\n", ctx->fd, (float)clock()/CLOCKS_PER_SEC - startTime); // request 31 - 1127 us, timer 60 - 262 us
            }

        }
    }
}

struct johnny_file johnny_slurps_file(const char* file_path, char* file_name) {
    char* file_ext = get_file_extension(file_name);

    const char* content_encoding = NULL;
    if (strlen(file_ext) > 0) {
        content_encoding = get_content_encoding(file_ext);
        if (content_encoding != NULL) {
            file_name[strlen(file_name) - strlen(file_ext) - 1] = '\0';
            file_ext = get_file_extension(file_name);
        }
    }
    const char* mime_type = get_mime_type(file_ext);
    char temp_header_buffer[1024];
    sprintf(temp_header_buffer, "HTTP/1.1 200 OK\r\nContent-Type: %s\r\n", mime_type);
    if (content_encoding != NULL)
        sprintf(temp_header_buffer + strlen(temp_header_buffer), "Content-Encoding: %s\r\n", content_encoding);

    unsigned char* buf;
    size_t file_size = slurp(file_path, &buf);
    sprintf(temp_header_buffer + strlen(temp_header_buffer), "Content-Length: %zu\r\n\r\n", file_size);

    const size_t response_length = strlen(temp_header_buffer) + file_size;
    char* response = malloc(response_length);
    memcpy(response, temp_header_buffer, response_length - file_size);
    memcpy(response + response_length - file_size, buf, file_size);
    free(buf);

    char* encoded_file_name_buf = url_encode(file_name);

    struct johnny_file file = { .response_length = response_length, .response = response, .url_encoded_file_name = encoded_file_name_buf };
    return file;
}

int johnny_slurps_files(char* base_dir, char* rel_dir, int file_counter) {
    DIR *d;
    struct dirent* dir_ent;
    char dir_path[strlen(base_dir) + strlen(rel_dir) + 1];
    strcpy(dir_path, base_dir);
    strcat(dir_path, rel_dir);
    d = opendir(dir_path);
    if (d) {
        while ((dir_ent = readdir(d)) != NULL)
            if (strcmp(dir_ent->d_name, ".") && strcmp(dir_ent->d_name, "..")) {
                struct stat sb;
                char full_path[strlen(dir_path) + strlen(dir_ent->d_name) + 2];
                strcpy(full_path, dir_path);
                strcat(full_path, "/");
                strcat(full_path, dir_ent->d_name);
                lstat(full_path, &sb);
                char rel_path[strlen(rel_dir) + strlen(dir_ent->d_name) + 2];
                strcpy(rel_path, rel_dir);
                if ((sb.st_mode & S_IFMT) == S_IFREG) {
                    strcat(rel_path, dir_ent->d_name);
                    JOHNNY_FILES[file_counter] = johnny_slurps_file(full_path, rel_path);
                    file_counter++;
                }
                else if ((sb.st_mode & S_IFMT) == S_IFDIR) {
                    strcat(rel_path, dir_ent->d_name);
                    strcat(rel_path, "/");
                    file_counter = johnny_slurps_files(base_dir, rel_path, file_counter);
                }
            }
        closedir(d);
    }
    return file_counter;
}

void reorder_johnny_files(int johnny_file_count) {
    struct johnny_file* reordered_johnny_files = malloc(johnny_file_count * sizeof(struct johnny_file));
    for (int move_from = 0; move_from < johnny_file_count; move_from++) {
        cmph_uint32 move_to = cmph_search(JOHNNY_HASH, JOHNNY_FILES[move_from].url_encoded_file_name, strlen(JOHNNY_FILES[move_from].url_encoded_file_name));
        reordered_johnny_files[move_to] = JOHNNY_FILES[move_from];
    }
    free(JOHNNY_FILES);
    JOHNNY_FILES = reordered_johnny_files;
}

int main(int argc, char* argv[]) {
    setvbuf(stdout, NULL, _IONBF, 0);

    int johnny_file_count = count_files(JOHNNY_ROOT);
    printf("johnny files: %d\n", johnny_file_count);
    if (johnny_file_count == 0)
        return EXIT_FAILURE;
    JOHNNY_FILES = malloc(johnny_file_count * sizeof(struct johnny_file));
    johnny_slurps_files(JOHNNY_ROOT, "", 0);

    // Creating a filled vector
    char* vector[johnny_file_count];
    int total_strings_length = 0;
    for (int file_counter = 0; file_counter < johnny_file_count; file_counter++) {
        total_strings_length += strlen(JOHNNY_FILES[file_counter].url_encoded_file_name) + 1;
    }
    printf("total file names length: %d\n", total_strings_length);
    char* contiguous_block_of_url_encoded_file_names = malloc(total_strings_length);
    int string_offset = 0;
    for (int file_counter = 0; file_counter < johnny_file_count; file_counter++) {
        int inclusive_string_length = strlen(JOHNNY_FILES[file_counter].url_encoded_file_name) + 1;
        memcpy(contiguous_block_of_url_encoded_file_names + string_offset, JOHNNY_FILES[file_counter].url_encoded_file_name, inclusive_string_length);
        free(JOHNNY_FILES[file_counter].url_encoded_file_name);
        JOHNNY_FILES[file_counter].url_encoded_file_name = contiguous_block_of_url_encoded_file_names + string_offset;
        vector[file_counter] = contiguous_block_of_url_encoded_file_names + string_offset;
        string_offset += inclusive_string_length;
    }
    printf("string offset after writing to memory: %d\n", string_offset);

    printf("creating minimal perfect hash function using the chd algorithm\n");
    cmph_io_adapter_t* source = cmph_io_vector_adapter(vector, johnny_file_count);
    printf("configuring cmph\n");
    cmph_config_t *config = cmph_config_new(source);
    printf("setting chd as algo to use\n");
    cmph_config_set_algo(config, CMPH_CHD);
    printf("creating hash function\n");
    JOHNNY_HASH = cmph_new(config);
    printf("destroying config\n");
    cmph_config_destroy(config);
    printf("reordering files\n");
    reorder_johnny_files(johnny_file_count);

    int core_cnt = sysconf(_SC_NPROCESSORS_ONLN);
    for (int core_id = 1; core_id < core_cnt; core_id++) {
        // create a new thread to handle client request
        pthread_t thread_id;
        pthread_create(&thread_id, NULL, johnny_worker, NULL);
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(core_id, &cpuset);
        pthread_setaffinity_np(thread_id, sizeof(cpu_set_t), &cpuset);
        pthread_detach(thread_id);
    }
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(0, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    johnny_worker();
}