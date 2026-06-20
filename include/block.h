/*
 * block.h - Block construction and hashing for the AHT blockchain.
 *
 * A Block bundles up to MAX_TX_PER_BLOCK transactions together with the header
 * fields that chain it to its predecessor and commit to its contents:
 *   - block_id          height of the block (genesis = 0)
 *   - timestamp         when the block was assembled
 *   - transaction_count number of transactions actually carried
 *   - previous_hash     block_hash of the parent (all-zero hex for genesis)
 *   - merkle_root       Merkle root over the carried transactions
 *   - nonce             value varied by the miner to satisfy the difficulty
 *   - miner_id          address credited with mining the block
 *   - difficulty        number of leading '0' hex digits the hash must have
 *   - block_hash        SHA-256 of the canonical header serialisation
 *
 * The block never trusts a stored merkle_root or block_hash blindly: both are
 * derived from the block's own contents (block_recompute_merkle_root,
 * block_compute_hash) so a tampered field is detectable.
 */
#ifndef AHT_BLOCK_H
#define AHT_BLOCK_H

#include "types.h"

/*
 * Recompute the Merkle root over the block's current transactions[0..count-1]
 * and store it in block->merkle_root. Returns 1 on success, 0 on bad input.
 */
int block_recompute_merkle_root(Block *block);

/*
 * Serialise the block header (block_id, timestamp, transaction_count,
 * previous_hash, merkle_root, nonce, miner_id, difficulty) into `out` as a
 * canonical, deterministic string. block_hash is excluded - it is the field
 * being derived. Returns the string length, or 0 on bad input.
 */
size_t block_serialize_header(const Block *block, char *out, size_t out_cap);

/*
 * Compute the block hash (SHA-256 of the header serialisation) and store it in
 * block->block_hash. The merkle_root field is used as-is; call
 * block_recompute_merkle_root first if the transaction set may have changed.
 * Returns 1 on success, 0 on bad input.
 */
int block_compute_hash(Block *block);

/*
 * True (1) if `hash` (a lower-case hex string) begins with at least
 * `difficulty` '0' characters - the proof-of-work target test. A difficulty of
 * 0 is always satisfied. Returns 0 on null input.
 */
int block_hash_meets_difficulty(const char *hash, int difficulty);

/*
 * Verify a block's internal consistency, independent of any chain context:
 *   - the stored merkle_root matches a fresh recomputation over its txs, and
 *   - the stored block_hash matches a fresh recomputation of its header.
 * Returns 1 if consistent, 0 otherwise (including null input).
 */
int block_verify_self(const Block *block);

#endif /* AHT_BLOCK_H */
