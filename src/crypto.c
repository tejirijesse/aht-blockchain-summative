/*
 * crypto.c - ECDSA (secp256k1) key generation, signing, verification, and
 * address derivation, implemented over OpenSSL 3's EC_KEY/ECDSA API.
 *
 * OpenSSL 3 marks the EC_KEY/ECDSA_do_sign family as deprecated in favour of
 * the EVP interface, but the low-level EC_KEY API is still the most direct way
 * to work with raw secp256k1 points and DER signatures. We suppress the
 * deprecation warnings so the project still builds cleanly under -Wall -Wextra.
 */
#define OPENSSL_SUPPRESS_DEPRECATED

#include "crypto.h"
#include "sha256.h"

#include <string.h>
#include <openssl/ec.h>
#include <openssl/obj_mac.h>
#include <openssl/bn.h>
#include <openssl/ecdsa.h>
#include <openssl/sha.h>

/* Lookup table for fast binary -> lower-case hex conversion. */
static const char HEX_DIGITS[] = "0123456789abcdef";

/* Write `len` raw bytes as lower-case hex into `out_hex` (>= len*2+1). */
static void bytes_to_hex(const unsigned char *bytes, size_t len, char *out_hex)
{
    for (size_t i = 0; i < len; i++) {
        out_hex[i * 2]     = HEX_DIGITS[(bytes[i] >> 4) & 0x0F];
        out_hex[i * 2 + 1] = HEX_DIGITS[bytes[i] & 0x0F];
    }
    out_hex[len * 2] = '\0';
}

/*
 * Convert one lower/upper-case hex digit to its 0-15 value, or -1 if `c` is
 * not a valid hex character.
 */
static int hex_val(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/*
 * Decode a null-terminated hex string into `out` (capacity `out_cap`). Returns
 * the number of bytes written, or -1 on malformed input / insufficient space.
 */
static int hex_to_bytes(const char *hex, unsigned char *out, size_t out_cap)
{
    size_t hex_len = strlen(hex);
    if (hex_len % 2 != 0) return -1;
    size_t n = hex_len / 2;
    if (n > out_cap) return -1;

    for (size_t i = 0; i < n; i++) {
        int hi = hex_val(hex[i * 2]);
        int lo = hex_val(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return -1;
        out[i] = (unsigned char)((hi << 4) | lo);
    }
    return (int)n;
}

int crypto_generate_keypair(KeyPair *out)
{
    int ok = 0;
    EC_KEY *key = NULL;
    const BIGNUM *priv_bn = NULL;
    const EC_GROUP *group = NULL;
    const EC_POINT *pub_point = NULL;
    unsigned char priv_bytes[32];
    unsigned char pub_bytes[65];

    if (out == NULL) return 0;

    key = EC_KEY_new_by_curve_name(NID_secp256k1);
    if (key == NULL) goto done;
    if (EC_KEY_generate_key(key) != 1) goto done;

    /* Private key: 32-byte big-endian scalar -> 64 hex chars. */
    priv_bn = EC_KEY_get0_private_key(key);
    if (priv_bn == NULL) goto done;
    memset(priv_bytes, 0, sizeof(priv_bytes));
    if (BN_bn2binpad(priv_bn, priv_bytes, (int)sizeof(priv_bytes)) != (int)sizeof(priv_bytes))
        goto done;
    bytes_to_hex(priv_bytes, sizeof(priv_bytes), out->private_key);

    /* Public key: uncompressed point (0x04 || X || Y), 65 bytes -> 130 hex. */
    group = EC_KEY_get0_group(key);
    pub_point = EC_KEY_get0_public_key(key);
    if (group == NULL || pub_point == NULL) goto done;
    {
        size_t written = EC_POINT_point2oct(group, pub_point,
                                            POINT_CONVERSION_UNCOMPRESSED,
                                            pub_bytes, sizeof(pub_bytes), NULL);
        if (written != sizeof(pub_bytes)) goto done;
    }
    bytes_to_hex(pub_bytes, sizeof(pub_bytes), out->public_key);

    /* Address: SHA-256 of the 65 public-key octets, first 20 bytes -> 40 hex. */
    {
        unsigned char digest[SHA256_DIGEST_LENGTH];
        SHA256(pub_bytes, sizeof(pub_bytes), digest);
        bytes_to_hex(digest, 20, out->address);
    }

    ok = 1;

done:
    if (key) EC_KEY_free(key);
    return ok;
}

int crypto_generate_keypair_from_seed(const char *seed, KeyPair *out)
{
    int ok = 0;
    EC_KEY *key = NULL;
    BIGNUM *priv_bn = NULL;
    BIGNUM *order = NULL;
    BN_CTX *bn_ctx = NULL;
    const EC_GROUP *group = NULL;
    EC_POINT *pub_point = NULL;
    unsigned char priv_bytes[32];
    unsigned char pub_bytes[65];
    unsigned char digest[SHA256_DIGEST_LENGTH];

    if (seed == NULL || out == NULL) return 0;

    SHA256((const unsigned char *)seed, strlen(seed), digest);

    key = EC_KEY_new_by_curve_name(NID_secp256k1);
    if (key == NULL) goto done;
    group = EC_KEY_get0_group(key);
    if (group == NULL) goto done;

    bn_ctx = BN_CTX_new();
    priv_bn = BN_bin2bn(digest, (int)sizeof(digest), NULL);
    order = BN_new();
    if (bn_ctx == NULL || priv_bn == NULL || order == NULL) goto done;
    if (EC_GROUP_get_order(group, order, bn_ctx) != 1) goto done;
    if (BN_mod(priv_bn, priv_bn, order, bn_ctx) != 1) goto done;
    if (BN_is_zero(priv_bn) && BN_one(priv_bn) != 1) goto done;
    if (EC_KEY_set_private_key(key, priv_bn) != 1) goto done;

    pub_point = EC_POINT_new(group);
    if (pub_point == NULL) goto done;
    if (EC_POINT_mul(group, pub_point, priv_bn, NULL, NULL, bn_ctx) != 1) goto done;
    if (EC_KEY_set_public_key(key, pub_point) != 1) goto done;

    memset(priv_bytes, 0, sizeof(priv_bytes));
    if (BN_bn2binpad(priv_bn, priv_bytes, (int)sizeof(priv_bytes)) != (int)sizeof(priv_bytes))
        goto done;
    bytes_to_hex(priv_bytes, sizeof(priv_bytes), out->private_key);

    {
        size_t written = EC_POINT_point2oct(group, pub_point,
                                            POINT_CONVERSION_UNCOMPRESSED,
                                            pub_bytes, sizeof(pub_bytes), NULL);
        if (written != sizeof(pub_bytes)) goto done;
    }
    bytes_to_hex(pub_bytes, sizeof(pub_bytes), out->public_key);
    if (!crypto_derive_address(out->public_key, out->address)) goto done;

    ok = 1;

done:
    if (pub_point) EC_POINT_free(pub_point);
    if (order) BN_free(order);
    if (priv_bn) BN_free(priv_bn);
    if (bn_ctx) BN_CTX_free(bn_ctx);
    if (key) EC_KEY_free(key);
    return ok;
}

int crypto_derive_address(const char *public_key_hex, char out_address[ADDRESS_LEN])
{
    unsigned char pub_bytes[65];
    unsigned char digest[SHA256_DIGEST_LENGTH];

    if (public_key_hex == NULL || out_address == NULL) return 0;

    /* Expect exactly the 65-byte uncompressed form (130 hex chars). */
    if (hex_to_bytes(public_key_hex, pub_bytes, sizeof(pub_bytes)) != (int)sizeof(pub_bytes))
        return 0;
    if (pub_bytes[0] != 0x04) return 0;

    SHA256(pub_bytes, sizeof(pub_bytes), digest);
    bytes_to_hex(digest, 20, out_address);
    return 1;
}

int crypto_sign(const char *private_key_hex, const void *data, size_t len,
                char out_sig_hex[SIG_HEX_LEN])
{
    int ok = 0;
    EC_KEY *key = NULL;
    BIGNUM *priv_bn = NULL;
    EC_GROUP *group = NULL;
    EC_POINT *pub_point = NULL;
    ECDSA_SIG *sig = NULL;
    unsigned char digest[SHA256_DIGEST_LENGTH];
    unsigned char *der = NULL;

    if (private_key_hex == NULL || out_sig_hex == NULL) return 0;
    if (data == NULL && len != 0) return 0;

    key = EC_KEY_new_by_curve_name(NID_secp256k1);
    if (key == NULL) goto done;

    /* Load the private scalar from hex. */
    priv_bn = BN_new();
    if (priv_bn == NULL) goto done;
    if (BN_hex2bn(&priv_bn, private_key_hex) == 0) goto done;
    if (EC_KEY_set_private_key(key, priv_bn) != 1) goto done;

    /* Derive and attach the matching public point (priv * G). */
    group = EC_GROUP_new_by_curve_name(NID_secp256k1);
    if (group == NULL) goto done;
    pub_point = EC_POINT_new(group);
    if (pub_point == NULL) goto done;
    if (EC_POINT_mul(group, pub_point, priv_bn, NULL, NULL, NULL) != 1) goto done;
    if (EC_KEY_set_public_key(key, pub_point) != 1) goto done;

    /* Hash the message, then ECDSA-sign the digest. */
    SHA256((const unsigned char *)data, len, digest);
    sig = ECDSA_do_sign(digest, (int)sizeof(digest), key);
    if (sig == NULL) goto done;

    /* DER-encode the signature and render it as hex. */
    {
        int der_len = i2d_ECDSA_SIG(sig, &der);
        if (der_len <= 0) goto done;
        if ((size_t)(der_len * 2 + 1) > SIG_HEX_LEN) goto done;
        bytes_to_hex(der, (size_t)der_len, out_sig_hex);
    }

    ok = 1;

done:
    if (der) OPENSSL_free(der);
    if (sig) ECDSA_SIG_free(sig);
    if (pub_point) EC_POINT_free(pub_point);
    if (group) EC_GROUP_free(group);
    if (priv_bn) BN_free(priv_bn);
    if (key) EC_KEY_free(key);
    return ok;
}

int crypto_verify(const char *public_key_hex, const void *data, size_t len,
                  const char *sig_hex)
{
    int ok = 0;
    EC_KEY *key = NULL;
    EC_GROUP *group = NULL;
    EC_POINT *pub_point = NULL;
    ECDSA_SIG *sig = NULL;
    unsigned char pub_bytes[65];
    unsigned char digest[SHA256_DIGEST_LENGTH];
    unsigned char der[SIG_HEX_LEN / 2];
    int der_len;

    if (public_key_hex == NULL || sig_hex == NULL) return 0;
    if (data == NULL && len != 0) return 0;

    /* Rebuild the public key from its uncompressed hex form. */
    if (hex_to_bytes(public_key_hex, pub_bytes, sizeof(pub_bytes)) != (int)sizeof(pub_bytes))
        return 0;
    if (pub_bytes[0] != 0x04) return 0;

    group = EC_GROUP_new_by_curve_name(NID_secp256k1);
    if (group == NULL) goto done;
    pub_point = EC_POINT_new(group);
    if (pub_point == NULL) goto done;
    if (EC_POINT_oct2point(group, pub_point, pub_bytes, sizeof(pub_bytes), NULL) != 1)
        goto done;

    key = EC_KEY_new_by_curve_name(NID_secp256k1);
    if (key == NULL) goto done;
    if (EC_KEY_set_public_key(key, pub_point) != 1) goto done;

    /* Decode the DER signature from hex. */
    der_len = hex_to_bytes(sig_hex, der, sizeof(der));
    if (der_len <= 0) goto done;
    {
        const unsigned char *p = der;
        sig = d2i_ECDSA_SIG(NULL, &p, (long)der_len);
        if (sig == NULL) goto done;
    }

    /* Hash the message and verify (1 = valid, 0 = invalid, -1 = error). */
    SHA256((const unsigned char *)data, len, digest);
    if (ECDSA_do_verify(digest, (int)sizeof(digest), sig, key) == 1)
        ok = 1;

done:
    if (sig) ECDSA_SIG_free(sig);
    if (key) EC_KEY_free(key);
    if (pub_point) EC_POINT_free(pub_point);
    if (group) EC_GROUP_free(group);
    return ok;
}
