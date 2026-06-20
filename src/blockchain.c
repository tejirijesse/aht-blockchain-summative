/*
 * blockchain.c - Chain assembly, linking, and verification for AHT.
 *
 * The chain is an ordered array of blocks inside ChainState. Block 0 is the
 * genesis block (all-zero previous_hash, no transactions); every later block i
 * carries previous_hash == blocks[i-1].block_hash and is internally consistent
 * (block_verify_self). Two derived fields - merkle_root and block_hash - are
 * always recomputed, never trusted as stored, so tampering is detectable.
 */
#include "blockchain.h"
#include "block.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 64 '0' hex characters + a terminating null: the genesis previous_hash. */
const char GENESIS_PREV_HASH[HASH_HEX_LEN] =
    "0000000000000000000000000000000000000000000000000000000000000000";

#define CHAIN_FILE_MAGIC   "AHTCHAIN"
#define CHAIN_FILE_VERSION 3u

typedef struct {
    char magic[8];
    uint32_t version;
} ChainFileHeader;

int blockchain_init(ChainState *chain, const char *miner_id,
                    int difficulty, double block_reward)
{
    Block *genesis;

    if (chain == NULL || miner_id == NULL) return 0;
    if (difficulty < 0) return 0;
    if (block_reward < 0.0) return 0;

    /* Start from a clean slate: zero blocks, empty world state. */
    memset(chain, 0, sizeof(*chain));
    chain->block_count = 0;
    chain->difficulty = difficulty;
    chain->last_retarget_block = 0;
    chain->block_reward = block_reward;
    chain->insurance_pool_balance = 0.0;
    chain->reinsurance_pool_balance = 0.0;

    /* Native token metadata. */
    strncpy(chain->token.token_name, "ALU Health Token",
            sizeof(chain->token.token_name) - 1);
    strncpy(chain->token.token_symbol, "AHT",
            sizeof(chain->token.token_symbol) - 1);
    chain->token.total_supply = 0.0;

    chain->mempool.count = 0;
    chain->utxos.count = 0;
    chain->account_count = 0;

    /*
     * Genesis block: height 0, no transactions, all-zero previous_hash. The
     * merkle_root is derived over the empty transaction set, then the header is
     * hashed. Difficulty is recorded but the genesis hash is accepted as-is (it
     * is the chain's anchor, not a mined block).
     */
    genesis = &chain->blocks[0];
    memset(genesis, 0, sizeof(*genesis));
    genesis->block_id = 0;
    genesis->timestamp = 0;
    genesis->transaction_count = 0;
    strncpy(genesis->previous_hash, GENESIS_PREV_HASH,
            sizeof(genesis->previous_hash) - 1);
    strncpy(genesis->miner_id, miner_id, sizeof(genesis->miner_id) - 1);
    genesis->difficulty = difficulty;
    genesis->nonce = 0;

    if (!block_recompute_merkle_root(genesis)) return 0;
    if (!block_compute_hash(genesis)) return 0;

    chain->block_count = 1;
    return 1;
}

const Block *blockchain_tip(const ChainState *chain)
{
    if (chain == NULL || chain->block_count <= 0) return NULL;
    return &chain->blocks[chain->block_count - 1];
}

int blockchain_make_next_block(const ChainState *chain, Block *out,
                               const Transaction *txs, int tx_count,
                               const char *miner_id, time_t now)
{
    const Block *tip;

    if (chain == NULL || out == NULL || miner_id == NULL) return 0;
    if (tx_count < 0 || tx_count > MAX_TX_PER_BLOCK) return 0;
    if (tx_count > 0 && txs == NULL) return 0;

    tip = blockchain_tip(chain);
    if (tip == NULL) return 0; /* must have a genesis to build on */

    memset(out, 0, sizeof(*out));
    out->block_id = (uint64_t)chain->block_count;
    out->timestamp = now;
    out->transaction_count = tx_count;
    strncpy(out->previous_hash, tip->block_hash,
            sizeof(out->previous_hash) - 1);
    strncpy(out->miner_id, miner_id, sizeof(out->miner_id) - 1);
    out->difficulty = chain->difficulty;
    out->nonce = 0;

    /* Copy the chosen transactions in, then commit to them via the root. */
    for (int i = 0; i < tx_count; i++)
        out->transactions[i] = txs[i];

    if (!block_recompute_merkle_root(out)) return 0;

    /* block_hash/nonce are left for the miner to fill via proof-of-work. */
    return 1;
}

int blockchain_append_block(ChainState *chain, const Block *block)
{
    const Block *tip;

    if (chain == NULL || block == NULL) return 0;
    if (chain->block_count >= MAX_BLOCKS) return 0;     /* chain full */
    if (chain->block_count <= 0) return 0;              /* no genesis yet */

    /* (a) internally consistent (merkle_root + block_hash recomputed). */
    if (!block_verify_self(block)) return 0;

    /* (b) height must be exactly the next slot. */
    if (block->block_id != (uint64_t)chain->block_count) return 0;

    /* (c) link to the current tip. */
    tip = blockchain_tip(chain);
    if (tip == NULL) return 0;
    if (strcmp(block->previous_hash, tip->block_hash) != 0) return 0;

    /* (d) proof-of-work target met for the chain's difficulty. */
    if (!block_hash_meets_difficulty(block->block_hash, chain->difficulty))
        return 0;

    chain->blocks[chain->block_count] = *block;
    chain->block_count++;
    return 1;
}

int blockchain_verify(const ChainState *chain)
{
    if (chain == NULL) return 0;
    if (chain->block_count <= 0) return 0;

    for (int i = 0; i < chain->block_count; i++) {
        const Block *b = &chain->blocks[i];

        /* Every block must be internally consistent. */
        if (!block_verify_self(b)) return 0;

        /* Heights are sequential from 0. */
        if (b->block_id != (uint64_t)i) return 0;

        if (i == 0) {
            /* Genesis carries the all-zero previous_hash. */
            if (strcmp(b->previous_hash, GENESIS_PREV_HASH) != 0) return 0;
        } else {
            const Block *prev = &chain->blocks[i - 1];

            /* Each previous_hash links to the prior block_hash. */
            if (strcmp(b->previous_hash, prev->block_hash) != 0) return 0;

            /* Mined blocks must meet their recorded difficulty. */
            if (!block_hash_meets_difficulty(b->block_hash, b->difficulty))
                return 0;
        }
    }

    return 1;
}

int blockchain_save(const ChainState *chain, const char *path)
{
    ChainFileHeader header;
    FILE *fp;

    if (chain == NULL || path == NULL || path[0] == '\0') return 0;

    fp = fopen(path, "wb");
    if (fp == NULL) return 0;

    memset(&header, 0, sizeof(header));
    memcpy(header.magic, CHAIN_FILE_MAGIC, sizeof(header.magic));
    header.version = CHAIN_FILE_VERSION;

    if (fwrite(&header, sizeof(header), 1, fp) != 1) {
        fclose(fp);
        return 0;
    }
    if (fwrite(chain, sizeof(*chain), 1, fp) != 1) {
        fclose(fp);
        return 0;
    }
    if (fclose(fp) != 0) return 0;
    return 1;
}

int blockchain_load(ChainState *chain, const char *path)
{
    ChainFileHeader header;
    ChainState *tmp;
    FILE *fp;

    if (chain == NULL || path == NULL || path[0] == '\0') return 0;

    tmp = malloc(sizeof(*tmp));
    if (tmp == NULL) return 0;

    fp = fopen(path, "rb");
    if (fp == NULL) {
        free(tmp);
        return 0;
    }

    if (fread(&header, sizeof(header), 1, fp) != 1) {
        fclose(fp);
        free(tmp);
        return 0;
    }
    if (memcmp(header.magic, CHAIN_FILE_MAGIC, sizeof(header.magic)) != 0) {
        fclose(fp);
        free(tmp);
        return 0;
    }
    if (header.version != CHAIN_FILE_VERSION) {
        fclose(fp);
        free(tmp);
        return 0;
    }
    if (fread(tmp, sizeof(*tmp), 1, fp) != 1) {
        fclose(fp);
        free(tmp);
        return 0;
    }
    if (fclose(fp) != 0) {
        free(tmp);
        return 0;
    }

    if (tmp->block_count <= 0 || tmp->block_count > MAX_BLOCKS) {
        free(tmp);
        return 0;
    }
    if (tmp->mempool.count < 0 || tmp->mempool.count > MAX_MEMPOOL) {
        free(tmp);
        return 0;
    }
    if (tmp->utxos.count < 0 || tmp->utxos.count > MAX_UTXOS) {
        free(tmp);
        return 0;
    }
    if (tmp->account_count < 0 || tmp->account_count > MAX_ACCOUNTS) {
        free(tmp);
        return 0;
    }
    if (tmp->policy_count < 0 || tmp->policy_count > MAX_POLICIES) {
        free(tmp);
        return 0;
    }
    if (tmp->difficulty < 0 || tmp->block_reward < 0.0) {
        free(tmp);
        return 0;
    }

    if (!blockchain_verify(tmp)) {
        free(tmp);
        return 0;
    }

    *chain = *tmp;
    free(tmp);
    return 1;
}
