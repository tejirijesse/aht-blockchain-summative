/*
 * sha256.c - Implementation of the SHA-256 hashing wrapper.
 *
 * Uses OpenSSL 3's one-shot SHA256() routine. Everything in the system funnels
 * through here so the on-the-wire hash representation (lower-case hex, 64 chars)
 * is identical for transaction ids, block hashes, and Merkle nodes.
 */
#include "sha256.h"

#include <string.h>
#include <openssl/sha.h>

/* Lookup table for fast binary -> lower-case hex conversion. */
static const char HEX_DIGITS[] = "0123456789abcdef";

/*
 * Write `len` raw bytes of `digest` as lower-case hex into `out_hex`, which
 * must hold at least (len*2 + 1) chars. Always null-terminates.
 */
static void bytes_to_hex(const unsigned char *digest, size_t len,
                         char *out_hex)
{
    for (size_t i = 0; i < len; i++) {
        out_hex[i * 2]     = HEX_DIGITS[(digest[i] >> 4) & 0x0F];
        out_hex[i * 2 + 1] = HEX_DIGITS[digest[i] & 0x0F];
    }
    out_hex[len * 2] = '\0';
}

void sha256_hex(const void *data, size_t len, char out_hex[HASH_HEX_LEN])
{
    unsigned char digest[SHA256_DIGEST_LENGTH];

    SHA256((const unsigned char *)data, len, digest);
    bytes_to_hex(digest, SHA256_DIGEST_LENGTH, out_hex);
}

void sha256_string(const char *str, char out_hex[HASH_HEX_LEN])
{
    sha256_hex(str, strlen(str), out_hex);
}

void sha256d_hex(const void *data, size_t len, char out_hex[HASH_HEX_LEN])
{
    unsigned char first[SHA256_DIGEST_LENGTH];
    unsigned char second[SHA256_DIGEST_LENGTH];

    /* Round one over the input, round two over the raw first digest. */
    SHA256((const unsigned char *)data, len, first);
    SHA256(first, SHA256_DIGEST_LENGTH, second);
    bytes_to_hex(second, SHA256_DIGEST_LENGTH, out_hex);
}
