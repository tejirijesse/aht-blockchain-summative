/*
 * block.c - Block construction and hashing for the AHT blockchain.
 *
 * A block's integrity rests on two derived fields that are never trusted as
 * stored: the merkle_root (a commitment to the carried transactions) and the
 * block_hash (SHA-256 of the canonical header). Both are recomputed from the
 * block's own contents, so any tampered field is detectable via
 * block_verify_self.
 *
 * The header serialisation deliberately excludes block_hash (the field being
 * derived) and uses explicit '|' separators with fixed integer widths so the
 * bytes are deterministic across platforms and runs.
 */
#include "block.h"
#include "merkle.h"
#include "sha256.h"

#include <stdio.h>
#include <string.h>

int block_recompute_merkle_root(Block *block)
{
    if (block == NULL) return 0;
    if (block->transaction_count < 0 ||
        block->transaction_count > MAX_TX_PER_BLOCK)
        return 0;

    /* Derive the root from the block's own transactions[0..count-1]. */
    return merkle_root(block->transactions, block->transaction_count,
                       block->merkle_root);
}

size_t block_serialize_header(const Block *block, char *out, size_t out_cap)
{
    int written;

    if (block == NULL || out == NULL || out_cap == 0) return 0;

    /*
     * Canonical header form. block_hash is intentionally omitted - it is the
     * value being derived from these bytes. Field order is fixed and stable:
     * block_id | timestamp | transaction_count | previous_hash | merkle_root |
     * nonce | miner_id | difficulty.
     */
    written = snprintf(out, out_cap,
                       "%llu|%lld|%d|%s|%s|%llu|%s|%d",
                       (unsigned long long)block->block_id,
                       (long long)block->timestamp,
                       block->transaction_count,
                       block->previous_hash,
                       block->merkle_root,
                       (unsigned long long)block->nonce,
                       block->miner_id,
                       block->difficulty);

    if (written < 0 || (size_t)written >= out_cap) return 0;
    return (size_t)written;
}

int block_compute_hash(Block *block)
{
    char buf[1024];
    size_t len;

    if (block == NULL) return 0;

    /* merkle_root is used as-is; caller recomputes it first if txs changed. */
    len = block_serialize_header(block, buf, sizeof(buf));
    if (len == 0) return 0;

    sha256_hex(buf, len, block->block_hash);
    return 1;
}

int block_hash_meets_difficulty(const char *hash, int difficulty)
{
    if (hash == NULL) return 0;
    if (difficulty <= 0) return 1; /* a zero/negative target is always met */

    /* Require at least `difficulty` leading '0' hex characters. */
    for (int i = 0; i < difficulty; i++) {
        if (hash[i] != '0') return 0;
    }
    return 1;
}

int block_verify_self(const Block *block)
{
    Block copy;
    char expected_root[HASH_HEX_LEN];
    char expected_hash[HASH_HEX_LEN];

    if (block == NULL) return 0;

    /*
     * Work on a copy so verification never mutates the caller's block. Recompute
     * the Merkle root from the transactions and compare against the stored one.
     */
    copy = *block;
    if (!block_recompute_merkle_root(&copy)) return 0;
    memcpy(expected_root, copy.merkle_root, sizeof(expected_root));
    if (strcmp(expected_root, block->merkle_root) != 0) return 0;

    /*
     * Recompute the header hash. The copy already carries the freshly derived
     * merkle_root (identical to the stored one at this point), so the header
     * bytes match what the block claims.
     */
    if (!block_compute_hash(&copy)) return 0;
    memcpy(expected_hash, copy.block_hash, sizeof(expected_hash));
    if (strcmp(expected_hash, block->block_hash) != 0) return 0;

    return 1;
}
