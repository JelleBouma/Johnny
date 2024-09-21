#include "johnny_worker_tls.h"
#include "johnny_global.h"
#include <sodium.h>
#include <string.h>

int parse_big_endian_uint24(const unsigned char* bytes) {
    return bytes[2] | (int)bytes[1] << 8 | (int)bytes[0] << 16;
}

int parse_big_endian_uint16(const unsigned char* bytes) {
    return bytes[1] | (int)bytes[0] << 8;
}

void print_tls_handshake_header(const unsigned char* buffer)
{
    const unsigned char handshake = *buffer;
    const unsigned short version = parse_big_endian_uint16(buffer + 1);
    const unsigned short size = parse_big_endian_uint16(buffer + 3);

    printf("%02hhX handshake, should be 16\r\n", handshake);
    printf("%04hX version\r\n", version);
    printf("%04hX size: %i\r\n\r\n", size, size);
}

typedef struct client_hello_data
{
    const unsigned char session_id_len;
    const unsigned char* session_id;
    const unsigned char cipher_suite;
} client_hello_data;

client_hello_data print_client_hello(const unsigned char* buf)
{
    const unsigned char handshake_type = *buf;
    const unsigned int size = parse_big_endian_uint24(buf + 1);
    const unsigned char* client_random = buf + 4;
    const unsigned char session_id_len = *(buf + 38);
    const unsigned char* session_id = buf + 39;
    const unsigned short cipher_suites_len = parse_big_endian_uint16(buf + 39 + session_id_len);
    const unsigned char* cipher_suites = buf + 41 + session_id_len;
    const unsigned short extensions_len = parse_big_endian_uint16(cipher_suites + cipher_suites_len + 2);
    const unsigned char* extensions = cipher_suites + cipher_suites_len + 4;

    unsigned short extension_type = parse_big_endian_uint16(extensions);
    while (extension_type != 0x0033) {
        const unsigned short extension_len = parse_big_endian_uint16(extensions + 2);
        extensions += extension_len + 4;
        extension_type = parse_big_endian_uint16(extensions);
    }
    const unsigned short key_length = parse_big_endian_uint16(extensions + 8);
    const unsigned char* client_pk = extensions + 10;

    printf("%02hhX handshake type, should be client hello (01)\r\n", handshake_type);
    printf("%06hX size: %i\r\n", size, size);

    for (int byte = 0; byte < 32; byte++)
        printf("%02hhX", client_random[byte]);
    printf(" 32 byte client random\r\n");

    if (session_id_len > 0) {
        for (int byte = 0; byte < session_id_len; byte++)
            printf("%02hhX", session_id[byte]);
        printf(" session id\r\n");
    }

    printf("%i cipher suites:\r\n", cipher_suites_len);
    for (int byte = 0; byte < cipher_suites_len; byte++)
        printf("%04hX\r\n", parse_big_endian_uint16(cipher_suites + byte * 2));

    unsigned char chosen_cipher = 0;
    for (int byte = 0; byte < cipher_suites_len; byte++)
        if (*(cipher_suites + byte) == 0x13)
        {
            chosen_cipher = *(cipher_suites + byte + 1);
            break;
        }
    printf("chosen cipher: %i\r\n", chosen_cipher);

    printf("%04hX preferred key exchange group\r\n", parse_big_endian_uint16(extensions + 6));
    printf("%04hX key length: %i\r\n", key_length, key_length);
    for (int byte = 0; byte < key_length; byte++)
        printf("%02hhX", parse_big_endian_uint16(extensions + 10 + byte));
    printf(" client public key\r\n");

    unsigned char server_pk[crypto_kx_PUBLICKEYBYTES], server_sk[crypto_kx_SECRETKEYBYTES];
    unsigned char shared_secret[crypto_scalarmult_curve25519_SCALARBYTES];
    const unsigned char zero[crypto_kdf_hkdf_sha256_KEYBYTES] = {0};
    unsigned char early_secret[crypto_kdf_hkdf_sha256_KEYBYTES], derived_early_secret[crypto_kdf_hkdf_sha256_KEYBYTES];
    unsigned char handshake_secret[crypto_kdf_hkdf_sha256_KEYBYTES], derived_handshake_secret[crypto_kdf_hkdf_sha256_KEYBYTES];
    unsigned char master_secret[crypto_kdf_hkdf_sha256_KEYBYTES];
    crypto_kdf_hkdf_sha256_extract(early_secret, zero, crypto_kdf_hkdf_sha256_KEYBYTES, zero, crypto_kdf_hkdf_sha256_KEYBYTES);
    crypto_kdf_hkdf_sha256_expand(derived_early_secret, crypto_kdf_hkdf_sha256_KEYBYTES, "tls13 derived", strlen("tls13 derived"), early_secret);
    /* Generate the server's key pair */
    crypto_kx_keypair(server_pk, server_sk);

    /* Prerequisite after this point: the client's public key must be known by the server */

    /* Compute two shared keys using the client's public key and the server's secret key.
       server_rx will be used by the server to receive data from the client,
       server_tx will be used by the server to send data to the client. */
    if (crypto_scalarmult_curve25519(shared_secret, server_sk, client_pk) != 0)
        perror("Suspicious client public key, bail out");

    crypto_kdf_hkdf_sha256_extract(handshake_secret, derived_early_secret, crypto_kdf_hkdf_sha256_KEYBYTES, shared_secret, crypto_scalarmult_curve25519_SCALARBYTES);
    crypto_kdf_hkdf_sha256_expand(derived_handshake_secret, crypto_kdf_hkdf_sha256_KEYBYTES, "tls13 derived", strlen("tls13 derived"), handshake_secret);
    crypto_kdf_hkdf_sha256_extract(master_secret, zero, crypto_kdf_hkdf_sha256_KEYBYTES, shared_secret, crypto_scalarmult_curve25519_SCALARBYTES);

    for (int byte = 0; byte < key_length; byte++)
        printf("%02hhX", early_secret[byte]);
    printf(" early secret\r\n");

    for (int byte = 0; byte < key_length; byte++)
        printf("%02hhX", derived_early_secret[byte]);
    printf(" derived early secret\r\n");

    for (int byte = 0; byte < key_length; byte++)
        printf("%02hhX", shared_secret[byte]);
    printf(" shared secret\r\n");

    for (int byte = 0; byte < key_length; byte++)
        printf("%02hhX", handshake_secret[byte]);
    printf(" handshake secret\r\n");

    for (int byte = 0; byte < key_length; byte++)
        printf("%02hhX", derived_handshake_secret[byte]);
    printf(" derived handshake secret\r\n");

    for (int byte = 0; byte < key_length; byte++)
        printf("%02hhX", master_secret[byte]);
    printf(" master secret\r\n");

    client_hello_data data = {.session_id_len = session_id_len, .session_id = session_id, .cipher_suite = chosen_cipher};
    return data;
}

void print_tls_handshake(const unsigned char* buffer, const short buffer_offset)
{
    print_tls_handshake_header(buffer + buffer_offset);
    print_client_hello(buffer + buffer_offset + 5);
}

void build_server_hello(client_hello_data* client_hello)
{
    unsigned char protocol = 0x16;
    unsigned short legacy_version = 0x0303;
    unsigned short handshake_length;
    unsigned char server_hello_type = 0x02;
    unsigned int server_hello_length;
    unsigned char server_random[32];
    unsigned char legacy_session_id_len = client_hello->session_id_len;
    unsigned char legacy_session_id[client_hello->session_id_len];
    unsigned char chosen_cipher_suite0 = 0x13;
    unsigned char chosen_cipher_suite1 = client_hello->cipher_suite;
    unsigned char legacy_compression_method = 0;
    unsigned short extensions_length;
    unsigned char* supported_versions_extension;
    unsigned char* key_share_extension;

    randombytes_buf(server_random, 32);
    memcpy(legacy_session_id, client_hello->session_id, client_hello->session_id_len);
}