#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include "johnny_config.h"
#include "johnny_global.h"
#include "file_io.h"

char* get_file_extension(const char *file_name) {
    char* dot = strrchr(file_name, '.');
    if (dot == NULL || dot == file_name)
        return "";
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
    struct dirent* dir_ent;
    DIR *d = opendir(dir_name);
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

johnny_file johnny_slurps_file(const char* file_path, char* file_name) {
    const char* file_ext = get_file_extension(file_name);

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
    const size_t file_size = slurp(file_path, &buf);
    sprintf(temp_header_buffer + strlen(temp_header_buffer), "Content-Length: %zu\r\n\r\n", file_size);

    const size_t response_length = strlen(temp_header_buffer) + file_size;
    char* response = malloc(response_length);
    memcpy(response, temp_header_buffer, response_length - file_size);
    memcpy(response + response_length - file_size, buf, file_size);
    free(buf);

    char* encoded_file_name_buf = url_encode(file_name);

    const johnny_file file = { .response_length = response_length, .response = response, .url_encoded_file_name = encoded_file_name_buf };
    return file;
}

int johnny_slurps_files(char* base_dir, const char* rel_dir, int file_counter) {
    struct dirent* dir_ent;
    char dir_path[strlen(base_dir) + strlen(rel_dir) + 1];
    strcpy(dir_path, base_dir);
    strcat(dir_path, rel_dir);
    DIR* d = opendir(dir_path);
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
    johnny_file* reordered_johnny_files = malloc(johnny_file_count * sizeof(struct johnny_file));
    for (int move_from = 0; move_from < johnny_file_count; move_from++) {
        const cmph_uint32 move_to = cmph_search(JOHNNY_HASH, JOHNNY_FILES[move_from].url_encoded_file_name, strlen(JOHNNY_FILES[move_from].url_encoded_file_name));
        reordered_johnny_files[move_to] = JOHNNY_FILES[move_from];
    }
    free(JOHNNY_FILES);
    JOHNNY_FILES = reordered_johnny_files;
}

int johnny_setup_files() {
    const int johnny_file_count = count_files(JOHNNY_ROOT);
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
        const int inclusive_string_length = strlen(JOHNNY_FILES[file_counter].url_encoded_file_name) + 1;
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

    return EXIT_SUCCESS;
}

void johnny_set_socket_limit_to_hard_limit() {
    struct rlimit rlimit;
    getrlimit(RLIMIT_NOFILE, &rlimit);
    rlimit.rlim_cur = rlimit.rlim_max;
    setrlimit(RLIMIT_NOFILE, &rlimit);
}

int johnny_setup_server() {
    johnny_set_socket_limit_to_hard_limit();
    // create server socket
    const int server_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
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
    const int yes = 1;
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

int johnny_setup_epoll() {
    const int epfd = epoll_create1(0);
    if (epfd == -1)
        perror("calling epoll_create1");
    return epfd;
}