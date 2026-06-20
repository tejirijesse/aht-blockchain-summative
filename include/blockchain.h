/*
 * blockchain.h - Chain assembly, linking, and verification for AHT.
 *
 * A ChainState carries an ordered array of blocks (blocks[0..block_count-1])
 * plus the surrounding world state (mempool, UTXO set, accounts, token,
 * difficulty, reward, and the two insurance pools). This module owns the chain
 * proper: creating the genesis block, appending a mined block, and verifying
 * the whole chain.
 *
 * Linking rule: block i (i > 0) carries previous_hash == blocks[i-1].block_hash,
 * and each block is internally consistent (block_verify_self). Genesis (height
 * 0) has an all-zero previous_hash and no parent.
 */
#ifndef AHT_BLOCKCHAIN_H
#define AHT_BLOCKCHAIN_H

#include "types.h"

/* The all-zero hex string used as the genesis block's previous_hash. */
extern const char GENESIS_PREV_HASH[HASH_HEX_LEN];

/*
 * Initialise a fresh chain: zero the state, set up the token and the starting
 * difficulty/reward, empty the mempool/UTXO set/accounts, and mine the genesis
 * block (height 0, no transactions, all-zero previous_hash). The genesis block
 * is hashed to satisfy `difficulty`. Returns 1 on success, 0 on bad input.
 */
int blockchain_init(ChainState *chain, const char *miner_id,
                    int difficulty, double block_reward);

/* The current chain height's tip block, or NULL if the chain is empty. */
const Block *blockchain_tip(const ChainState *chain);

/*
 * Assemble (but do not yet mine) the next block: set its block_id to the next
 * height, timestamp to `now`, previous_hash to the tip's block_hash, copy in up
 * to MAX_TX_PER_BLOCK transactions, derive the merkle_root, and stamp the
 * miner_id and difficulty. block_hash/nonce are left for the miner to fill.
 * Returns 1 on success, 0 on bad input (null/over-capacity/empty chain).
 */
int blockchain_make_next_block(const ChainState *chain, Block *out,
                               const Transaction *txs, int tx_count,
                               const char *miner_id, time_t now);

/*
 * Append an already-mined block to the chain. The block must (a) be internally
 * consistent (block_verify_self), (b) carry block_id == block_count, (c) link
 * to the current tip via previous_hash, and (d) meet the chain's difficulty.
 * Returns 1 on success, 0 if any check fails or the chain is full.
 */
int blockchain_append_block(ChainState *chain, const Block *block);

/*
 * Verify the entire chain: every block is internally consistent, heights are
 * sequential from 0, each previous_hash matches the prior block_hash, the
 * genesis previous_hash is all-zero, and every block meets its difficulty.
 * Returns 1 if the whole chain is valid, 0 otherwise (including null input).
 */
int blockchain_verify(const ChainState *chain);

/*
 * Persist the complete ChainState as a binary snapshot. The file contains a
 * small header (magic + format version) followed by the raw ChainState bytes.
 * Returns 1 on success, 0 on bad input or I/O failure.
 */
int blockchain_save(const ChainState *chain, const char *path);

/*
 * Load a ChainState from a binary snapshot previously written by
 * blockchain_save(). The loaded state is fully verified before being accepted.
 * Returns 1 on success, 0 on bad input, format mismatch, I/O failure, or
 * failed chain verification.
 */
int blockchain_load(ChainState *chain, const char *path);

#endif /* AHT_BLOCKCHAIN_H */
