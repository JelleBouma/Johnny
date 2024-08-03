#include <arpa/inet.h>
#include <dirent.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "file_io.h"
#include "cmph/src/cmph.h"

struct johnny_file {
    char* url_encoded_file_name;
    char* response;
    size_t response_length;
};

struct johnny_file* johnny_files;
cmph_t* johnny_hash;

const char* get_file_extension(const char *file_name) {
    const char *dot = strrchr(file_name, '.');
    if (!dot || dot == file_name) {
        return "";
    }
    return dot + 1;
}

char* get_file_name_from_request(char *request) {
    strtok(request + 5, " ");
    return request + 5;
}

char* url_encode(const char* originalText)
{
    // allocate memory for the worst possible case (all characters need to be encoded)
    char *encodedText = (char *)malloc(sizeof(char)*strlen(originalText)*3+1);

    const char *hex = "0123456789abcdef";

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

unsigned int count_files(char* dir_name) {
    printf("counting files in directory %s\n", dir_name);
    unsigned int file_count = 0;
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

void johnny_handles_request(int* client_fd) {
    size_t buffer_size = 262;
    char buffer[262];

    // receive request data from client and store into buffer
    ssize_t bytes_received = 1;
    while (bytes_received > 0) {
        bytes_received = recv(*client_fd, buffer, buffer_size, 0);
        if (bytes_received > 0) {
            // find file
            char* file_name = get_file_name_from_request(buffer);
            unsigned int index = cmph_search(johnny_hash, file_name, strlen(file_name));
            struct johnny_file johnny_file = johnny_files[index];

            // build response
            const bool found = !strcmp(file_name, johnny_file.url_encoded_file_name);
            const char* response = found ? johnny_file.response : "HTTP/1.1 404 Not Found\r\n\r\n";
            const size_t response_length = found ? johnny_file.response_length : strlen(response);

            // send HTTP response to client
            size_t sentBytes = 0;
            while (sentBytes < response_length)
                sentBytes += write(*client_fd, response + sentBytes, response_length - sentBytes);
        }
    }
    free(client_fd);
}

struct johnny_file johnny_slurps_file(const char* file_path, const char* file_name) {
    printf("reading %s, ", file_name);
    const char* file_ext = get_file_extension(file_name);
    const char* mime_type = get_mime_type(file_ext);
    const char* header_format = "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-length: %lu\r\n\r\n";

    unsigned char* buf;
    printf("slurping, ");
    size_t file_size = slurp(file_path, &buf);
    printf("slurped, ");

    char tempHeaderBuffer[1024];
    sprintf(tempHeaderBuffer, header_format, mime_type, file_size);
    const size_t response_length = strlen(tempHeaderBuffer) + file_size;
    char* response = malloc(response_length);
    memcpy(response, tempHeaderBuffer, response_length - file_size);
    memcpy(response + response_length - file_size, buf, file_size);
    free(buf);

    printf("url encoding, ");
    char* encoded_file_name_buf = url_encode(file_name);
    printf("url encoded %s\n", encoded_file_name_buf);

    struct johnny_file file = { .response_length = response_length, .response = response, .url_encoded_file_name = encoded_file_name_buf };
    return file;
}

unsigned int johnny_slurps_files(char* base_dir, char* rel_dir, unsigned int file_counter) {
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
                    johnny_files[file_counter] = johnny_slurps_file(full_path, rel_path);
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

void reorder_johnny_files(unsigned int johnny_file_count) {
    struct johnny_file* reordered_johnny_files = malloc(johnny_file_count * sizeof(struct johnny_file));
    for (int move_from = 0; move_from < johnny_file_count; move_from++) {
        unsigned int move_to = cmph_search(johnny_hash, johnny_files[move_from].url_encoded_file_name, strlen(johnny_files[move_from].url_encoded_file_name));
        reordered_johnny_files[move_to] = johnny_files[move_from];
    }
    free(johnny_files);
    johnny_files = reordered_johnny_files;
}

int main(int argc, char* argv[]) {
    setvbuf(stdout, NULL, _IONBF, 0);
    int port = atoi(argv[1]);
    char* dir_name = argv[2];
    int server_fd;
    struct sockaddr_in server_addr;

    unsigned int johnny_file_count = count_files(dir_name);
    printf("johnny files: %d\n", johnny_file_count);
    if (johnny_file_count == 0)
        return EXIT_FAILURE;
    johnny_files = malloc(johnny_file_count * sizeof(struct johnny_file));
    johnny_slurps_files(dir_name, "", 0);

    // Creating a filled vector
    char* vector[johnny_file_count];
    unsigned int total_strings_length = 0;
    for (int file_counter = 0; file_counter < johnny_file_count; file_counter++) {
        total_strings_length += strlen(johnny_files[file_counter].url_encoded_file_name) + 1;
    }
    printf("total file names length: %d\n", total_strings_length);
    char* contiguous_block_of_url_encoded_file_names = malloc(total_strings_length);
    unsigned int string_offset = 0;
    for (int file_counter = 0; file_counter < johnny_file_count; file_counter++) {
        unsigned int inclusive_string_length = strlen(johnny_files[file_counter].url_encoded_file_name) + 1;
        memcpy(contiguous_block_of_url_encoded_file_names + string_offset, johnny_files[file_counter].url_encoded_file_name, inclusive_string_length);
        free(johnny_files[file_counter].url_encoded_file_name);
        johnny_files[file_counter].url_encoded_file_name = contiguous_block_of_url_encoded_file_names + string_offset;
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
    johnny_hash = cmph_new(config);
    printf("destroying config\n");
    cmph_config_destroy(config);

    //Find key
    printf("reordering files\n");
    reorder_johnny_files(johnny_file_count);
    printf("finding keys\n");
    unsigned int i = 0;
    while (i < johnny_file_count) {
        const char *key = vector[i];
        unsigned int id = cmph_search(johnny_hash, key, strlen(key));
        printf("key:%s -- hash:%u\n", key, id);
        i++;
    }


    // create server socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // config socket
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);
    memset(server_addr.sin_zero, 0, 8);

    // bind socket to port
    if (bind(server_fd,
            (struct sockaddr *)&server_addr,
            sizeof(server_addr)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    // listen for connections
    if (listen(server_fd, 256) < 0) {
        perror("listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Johnny listening on port %d\n", port);
    while (true) {
        // client info
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        int* client_fd = malloc(sizeof(int));

        // accept client connection
        if ((*client_fd = accept(server_fd,
                                (struct sockaddr *)&client_addr,
                                &client_addr_len)) < 0) {
            perror("accept failed");
            continue;
        }

        // create a new thread to handle client request
        pthread_t thread_id;
        pthread_create(&thread_id, NULL, johnny_handles_request, client_fd);
        pthread_detach(thread_id);
    }
}