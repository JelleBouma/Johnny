#include "johnny_worker_tls.h"
#include "johnny_global.h"

int parse_big_endian_uint24(const unsigned char* bytes) {
    return bytes[2] | (int)bytes[1] << 8 | (int)bytes[0] << 16;
}

int parse_big_endian_uint16(const unsigned char* bytes) {
    return bytes[1] | (int)bytes[0] << 8;
}

void print_tls_handshake_header(const unsigned char* buffer, short buffer_offset)
{
    const unsigned char handshake = *buffer;
    const unsigned short version = parse_big_endian_uint16(buffer + 1);
    const unsigned short size = parse_big_endian_uint16(buffer + 3);

    printf("%02hhX handshake, should be 16\r\n", handshake);
    printf("%04hX version\r\n", version);
    printf("%04hX size: %i\r\n\r\n", size, size);
}

void print_client_hello(const unsigned char* buffer, const short buffer_offset)
{
    const unsigned char handshake_type = *buffer;
    const unsigned short size = parse_big_endian_uint16(buffer + 1);

    printf("%02hhX handshake type, should be client hello (01)\r\n", handshake_type);
    printf("%04hX size: %i\r\n\r\n", size, size);
}

void print_tls_handshake(const unsigned char* buffer, const short buffer_offset)
{
    print_tls_handshake_header(buffer, buffer_offset);
    print_client_hello(buffer, buffer_offset);
}