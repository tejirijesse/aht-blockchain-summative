/*
 * mining.h - Proof-of-Work mining for the AHT blockchain.
 *
 * Mining turns an assembled-but-unsealed block into a valid one by searching
 * for a nonce whose resulting block_hash meets the chain's difficulty target
 * (block_hash_meets_difficulty). The miner is paid via a coinbase transaction
 * (TX_COINBASE, value chain->block_reward) prepended to the block's body, so
 * the reward is committed to by the merkle_root like any other transaction.
 *
 * Two organisational modes are supported:
 *   - MINE_SOLO: the miner address receives the full block reward directly.
 *   - MINE_POOL: the supplied address is a pool payout address that receives
 *     the reward on behalf of the contributing miners.
 * The proof-of-work itself is identical in both modes; the mode only records
 * who the coinbase credits.
 *
 * Difficulty retargeting keeps the average block time near a target by
 * comparing the wall-clock span of the most recent window of blocks against
 * the expected span and nudging chain->difficulty up or down.
 */
#ifndef AHT_MINING_H
#define AHT_MINING_H

#include "types.h"

/* Number of blocks between difficulty adjustments. */
#define MINING_RETARGET_INTERVAL 10

/* Desired average seconds between blocks (used by retargeting). */
#define MINING_TARGET_BLOCK_TIME 30

/* Default ceiling on nonce attempts when the caller passes 0. */
#define MINING_DEFAULT_MAX_ATTEMPTS 100000000ULL

/* Lowest difficulty retargeting will ever fall to. */
#define MINING_MIN_DIFFICULTY 1

/* Outcome of a mining attempt. */
typedef struct {
    int found;                      /* 1 if a qualifying nonce was located */
    uint64_t nonce;                 /* the winning nonce (valid iff found) */
    uint64_t attempts;              /* number of hashes computed */
    char block_hash[HASH_HEX_LEN];  /* the winning block_hash (iff found) */
} MiningResult;

/*
 * Brute-force a nonce for an already-assembled block until its recomputed
 * block_hash satisfies `difficulty` leading zero hex digits. Scans nonces
 * 0..max_attempts-1 (0 selects MINING_DEFAULT_MAX_ATTEMPTS). On success the
 * block carries the winning nonce/block_hash and `result` is filled in.
 * Returns 1 if a nonce was found, 0 otherwise (including bad input).
 */
int mining_proof_of_work(Block *block, int difficulty,
                         uint64_t max_attempts, MiningResult *result);

/*
 * Assemble, mine, and append the next block in one step. A coinbase paying
 * chain->block_reward to `miner_id` is prepended to the (optional) selected
 * transactions, the block is linked to the tip, proof-of-work is performed at
 * the chain's current difficulty, and the sealed block is appended. The token
 * total_supply grows by the minted reward on success. Returns 1 on success, 0
 * on bad input, a failed search, or a rejected append.
 */
int mining_mine_block(ChainState *chain, const Transaction *txs, int tx_count,
                      const char *miner_id, MiningMode mode, time_t now,
                      MiningResult *result);

/*
 * Adjust chain->difficulty based on how long the most recent window of
 * MINING_RETARGET_INTERVAL blocks actually took versus the expected span
 * (interval * MINING_TARGET_BLOCK_TIME). Difficulty rises if blocks came too
 * fast, falls (never below MINING_MIN_DIFFICULTY) if too slow. A no-op until
 * the chain has at least one full window. Returns the (possibly unchanged)
 * difficulty, or 0 on null input.
 */
int mining_retarget_difficulty(ChainState *chain);

#endif /* AHT_MINING_H */
