/*
 * merkle.h - Merkle tree construction over a block's transactions.
 *
 * Implemented from scratch (no external Merkle libraries). The tree is built
 * bottom-up from transaction leaf hashes:
 *
 *   - Leaf hash      : SHA-256 of a transaction's canonical serialised fields.
 *   - Internal node  : double SHA-256 of the concatenation of its two child
 *                      hex hashes (Bitcoin-style, via sha256d_hex).
 *   - Odd row        : when a level has an odd number of nodes, the last node
 *                      is duplicated (paired with itself) before hashing up.
 *
 * All hashes travel as lower-case hex strings (HASH_HEX_LEN, including the
 * null terminator), matching the rest of the system so a block's merkle_root
 * compares like-with-like against recomputed values during verification.
 */
#ifndef AHT_MERKLE_H
#define AHT_MERKLE_H

#include "types.h"

/*
 * Compute the leaf hash for a single transaction into `out_hex` (must be at
 * least HASH_HEX_LEN). The hash covers the transaction's identifying fields
 * (sender, receiver, amount, type, timestamp, nonce, references) so any
 * tampering with a confirmed transaction changes the Merkle root.
 */
void merkle_leaf_hash(const Transaction *tx, char out_hex[HASH_HEX_LEN]);

/*
 * Build the Merkle root for `count` transactions and write it as hex into
 * `out_root` (at least HASH_HEX_LEN). Returns 1 on success, 0 on bad input.
 *
 * Conventions:
 *   - count == 0 yields the SHA-256 of the empty string (a stable, well-defined
 *     root for empty blocks) and returns 1.
 *   - When a level has an odd node count, the final hash is duplicated.
 */
int merkle_root(const Transaction *txs, int count, char out_root[HASH_HEX_LEN]);

#endif /* AHT_MERKLE_H */
