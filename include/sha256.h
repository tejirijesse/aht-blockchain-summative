/*
 * sha256.h - Thin wrapper over OpenSSL 3's SHA-256.
 *
 * The rest of the system never touches OpenSSL directly for hashing; it asks
 * here for a hex digest. Centralising this keeps the hashing format (lower-case
 * hex, 64 chars) identical everywhere a hash is computed (tx ids, block hashes,
 * Merkle nodes) so links and verification compare like with like.
 */
#ifndef AHT_SHA256_H
#define AHT_SHA256_H

#include <stddef.h>
#include "types.h"

/*
 * Hash `len` bytes at `data` and write the result as a null-terminated
 * lower-case hex string (64 chars + '\0') into `out_hex`. `out_hex` must be
 * at least HASH_HEX_LEN bytes.
 */
void sha256_hex(const void *data, size_t len, char out_hex[HASH_HEX_LEN]);

/*
 * Convenience wrapper for hashing a C string (uses strlen, excludes the
 * terminating null from the hashed bytes).
 */
void sha256_string(const char *str, char out_hex[HASH_HEX_LEN]);

/*
 * Double SHA-256 (sha256(sha256(data))), returned as hex. Used where a
 * Bitcoin-style two-round hash is desired (e.g. Merkle nodes) to resist
 * length-extension concerns.
 */
void sha256d_hex(const void *data, size_t len, char out_hex[HASH_HEX_LEN]);

#endif /* AHT_SHA256_H */
