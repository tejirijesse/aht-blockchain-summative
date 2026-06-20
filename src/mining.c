/*
 * mining.c - Proof-of-Work mining for the AHT blockchain.
 *
 * This module formalises the nonce search used to seal blocks, packages the
 * full "assemble + mine + append" path (paying the miner via a coinbase), and
 * implements difficulty retargeting that keeps the average block time near
 * MINING_TARGET_BLOCK_TIME.
 *
 * The proof-of-work is a straightforward brute-force scan over the nonce field:
 * for each candidate nonce we recompute the block_hash (SHA-256 of the canonical
 * header) and stop at the first hash that carries `difficulty` leading zero hex
 * digits (block_hash_meets_difficulty). The merkle_root is *not* recomputed in
 * the inner loop - it is a commitment to the transactions, which do not change
 * while we vary the nonce, so the caller commits to it once up front.
 */
#include "mining.h"
#include "block.h"
#include "blockchain.h"
#include "transaction.h"

#include <stdio.h>
#include <string.h>

int mining_proof_of_work(Block *block, int difficulty,
                         uint64_t max_attempts, MiningResult *result)
{
    uint64_t n;

    if (block == NULL || result == NULL) return 0;

    /* Start from a clean result so partial state is never mistaken for a win. */
    memset(result, 0, sizeof(*result));

    /* A caller-supplied 0 selects the default ceiling on nonce attempts. */
    if (max_attempts == 0) max_attempts = MINING_DEFAULT_MAX_ATTEMPTS;

    /*
     * Scan nonces 0..max_attempts-1. The merkle_root is assumed already valid
     * for the block's transactions (set by blockchain_make_next_block); only
     * the nonce - and therefore the header bytes and resulting hash - changes.
     */
    for (n = 0; n < max_attempts; n++) {
        block->nonce = n;

        /* Recompute the header hash for this nonce. */
        if (!block_compute_hash(block)) return 0;

        result->attempts = n + 1;

        if (block_hash_meets_difficulty(block->block_hash, difficulty)) {
            result->found = 1;
            result->nonce = n;
            memcpy(result->block_hash, block->block_hash,
                   sizeof(result->block_hash));
            return 1;
        }
    }

    /* Exhausted the budget without a qualifying nonce. */
    return 0;
}

int mining_mine_block(ChainState *chain, const Transaction *txs, int tx_count,
                      const char *miner_id, MiningMode mode, time_t now,
                      MiningResult *result)
{
    Block block;
    Transaction body[MAX_TX_PER_BLOCK];
    Transaction coinbase;
    int body_count;
    int i;

    (void)mode; /* PoW is identical for solo/pool; mode only labels the payee. */

    if (chain == NULL || miner_id == NULL || result == NULL) return 0;
    if (tx_count < 0) return 0;
    if (tx_count > 0 && txs == NULL) return 0;

    /*
     * The coinbase is prepended to the block body, so the total number of
     * transactions is tx_count + 1. Reject anything that would overflow a block.
     */
    if (tx_count + 1 > MAX_TX_PER_BLOCK) return 0;

    /*
     * Build the coinbase paying chain->block_reward to the miner/pool address.
     * In MINE_SOLO the address is the miner itself; in MINE_POOL it is the pool
     * payout address - either way the coinbase simply credits `miner_id`.
     */
    if (!tx_make_coinbase(&coinbase, miner_id, chain->block_reward, now))
        return 0;

    /* Body = [coinbase, txs...]; the reward is committed to by the merkle_root. */
    body[0] = coinbase;
    body_count = 1;
    for (i = 0; i < tx_count; i++)
        body[body_count++] = txs[i];

    /* Assemble (link to tip, derive merkle_root) but leave nonce/hash unmined. */
    if (!blockchain_make_next_block(chain, &block, body, body_count,
                                    miner_id, now))
        return 0;

    /* Search for a nonce that meets the chain's current difficulty. */
    if (!mining_proof_of_work(&block, chain->difficulty, 0, result))
        return 0;

    /* Append the sealed block; this re-verifies linkage, PoW, and self-consistency. */
    if (!blockchain_append_block(chain, &block)) return 0;

    /* Minting succeeded: the new reward enters circulation. */
    chain->token.total_supply += chain->block_reward;

    return 1;
}

int mining_retarget_difficulty(ChainState *chain)
{
    const Block *last;
    const Block *first;
    double average_time;
    int old_difficulty;
    int new_difficulty;

    if (chain == NULL) return 0;

    if (chain->block_count <= MINING_RETARGET_INTERVAL + 1)
        return chain->difficulty;
    if (chain->last_retarget_block > 0 &&
        chain->block_count - 1 < chain->last_retarget_block + MINING_RETARGET_INTERVAL)
        return chain->difficulty;

    last = &chain->blocks[chain->block_count - 1];
    first = &chain->blocks[chain->block_count - 1 - MINING_RETARGET_INTERVAL];
    average_time = (double)(last->timestamp - first->timestamp) /
                   (double)MINING_RETARGET_INTERVAL;

    old_difficulty = chain->difficulty;
    new_difficulty = old_difficulty;

    if (average_time < 30.0) {
        new_difficulty += 1;
    } else if (average_time > 90.0) {
        if (new_difficulty > MINING_MIN_DIFFICULTY)
            new_difficulty -= 1;
    }

    chain->difficulty = new_difficulty;
    chain->last_retarget_block = chain->block_count - 1;

    if (old_difficulty != new_difficulty) {
        printf("difficulty retarget at block %d: old=%d new=%d average_block_time=%.2f\n",
               chain->last_retarget_block, old_difficulty, new_difficulty, average_time);
    } else {
        printf("difficulty retarget at block %d: unchanged=%d average_block_time=%.2f\n",
               chain->last_retarget_block, new_difficulty, average_time);
    }

    return chain->difficulty;
}
