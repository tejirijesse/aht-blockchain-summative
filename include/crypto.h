/*
 * crypto.h - ECDSA digital-signature and address utilities.
 *
 * Wraps OpenSSL 3's EC_KEY/ECDSA API on the secp256k1 curve (the same curve
 * Bitcoin uses). The rest of the system only ever deals with the hex string
 * forms defined here, so no other module needs to include OpenSSL EC headers.
 *
 * Key/address formats:
 *   - private key: 32 raw bytes -> 64 lower-case hex chars + '\0'
 *   - public key : uncompressed EC point (0x04 || X || Y), 65 bytes ->
 *                  130 hex chars + '\0'
 *   - address    : SHA-256 of the uncompressed public-key octets, truncated to
 *                  the first 20 bytes -> 40 hex chars + '\0'
 *   - signature  : DER-encoded ECDSA signature rendered as hex + '\0'
 */
#ifndef AHT_CRYPTO_H
#define AHT_CRYPTO_H

#include "types.h"

/* 32-byte secp256k1 private key as hex (64 chars) + null terminator. */
#define PRIVKEY_HEX_LEN 65

/*
 * A complete key pair plus the derived address. All three members are
 * null-terminated hex strings (see formats above).
 */
typedef struct {
    char private_key[PRIVKEY_HEX_LEN];
    char public_key[PUBKEY_HEX_LEN];
    char address[ADDRESS_LEN];
} KeyPair;

/*
 * Generate a fresh secp256k1 key pair and fill `out`. Returns 1 on success,
 * 0 on failure (e.g. OpenSSL error). On failure `out` is left unspecified.
 */
int crypto_generate_keypair(KeyPair *out);

/*
 * Deterministically derive a secp256k1 key pair from a seed string. This is
 * used for reproducible internal/system wallets. Returns 1 on success, 0 on
 * failure.
 */
int crypto_generate_keypair_from_seed(const char *seed, KeyPair *out);

/*
 * Derive the 40-hex-char address for a given uncompressed public-key hex
 * string into `out_address` (must be at least ADDRESS_LEN). Returns 1 on
 * success, 0 if the public-key hex is malformed.
 */
int crypto_derive_address(const char *public_key_hex, char out_address[ADDRESS_LEN]);

/*
 * Sign `len` bytes of `data` with the given private-key hex. The data is
 * SHA-256 hashed first, then signed with ECDSA; the DER-encoded signature is
 * written as hex into `out_sig_hex` (must be at least SIG_HEX_LEN). Returns 1
 * on success, 0 on failure.
 */
int crypto_sign(const char *private_key_hex, const void *data, size_t len,
                char out_sig_hex[SIG_HEX_LEN]);

/*
 * Verify a hex DER signature over `len` bytes of `data` against the given
 * uncompressed public-key hex. Returns 1 if the signature is valid, 0 if it
 * is invalid or any input is malformed.
 */
int crypto_verify(const char *public_key_hex, const void *data, size_t len,
                  const char *sig_hex);

#endif /* AHT_CRYPTO_H */
