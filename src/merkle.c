/*
 * merkle.c - From-scratch Merkle tree over a block's transactions.
 *
 * Build strategy (bottom-up):
 *   1. Each transaction is reduced to a leaf hash via merkle_leaf_hash(): the
 *      SHA-256 of a canonical, deterministic serialisation of its identifying
 *      fields. Two transactions hash to the same leaf only if every covered
 *      field matches, so tampering with a confirmed transaction changes its
 *      leaf and therefore the root.
 *   2. Adjacent leaf hex hashes are combined into parents using sha256d_hex
 *      (Bitcoin-style double SHA-256) over the concatenation of the two child
 *      hex strings. When a level has an odd number of nodes, the final node is
 *      duplicated (paired with itself) before hashing up a level.
 *   3. The process repeats until a single hash remains: the Merkle root.
 *
 * The empty-block convention (count == 0) is the SHA-256 of the empty string,
 * giving a stable, well-defined root rather than an undefined value.
 *
 * No external Merkle libraries are used; only the project's own SHA-256 helpers.
 */
#include "merkle.h"
#include "sha256.h"

#include <stdio.h>
#include <string.h>

void merkle_leaf_hash(const Transaction *tx, char out_hex[HASH_HEX_LEN])
{
    char buf[1024];

    if (tx == NULL) {
        /* Defensive: hash the empty string for a null transaction. */
        sha256_hex("", 0, out_hex);
        return;
    }

    /*
     * Canonical serialisation: a fixed field order with explicit separators so
     * different field values can never collide into the same byte string. The
     * amount uses a fixed precision to stay deterministic across platforms.
     */
    snprintf(buf, sizeof(buf),
             "%s|%s|%s|%s|%.8f|%d|%lld|%llu|%lld|%s",
             tx->transaction_id,
             tx->sender_address,
             tx->receiver_address,
             tx->related_member_address,
             tx->amount,
             (int)tx->transaction_type,
             (long long)tx->timestamp,
             (unsigned long long)tx->sender_nonce,
             (long long)tx->policy_expiry,
             tx->ref_transaction_id);

    sha256_hex(buf, strlen(buf), out_hex);
}

int merkle_root(const Transaction *txs, int count, char out_root[HASH_HEX_LEN])
{
    /* Two working levels of the tree, swapped each round. */
    static char level_a[MAX_TX_PER_BLOCK][HASH_HEX_LEN];
    static char level_b[MAX_TX_PER_BLOCK][HASH_HEX_LEN];
    char (*cur)[HASH_HEX_LEN] = level_a;
    char (*next)[HASH_HEX_LEN] = level_b;
    int n;

    if (out_root == NULL) return 0;
    if (count < 0 || count > MAX_TX_PER_BLOCK) return 0;
    if (count > 0 && txs == NULL) return 0;

    /* Empty block: SHA-256 of the empty string. */
    if (count == 0) {
        sha256_hex("", 0, out_root);
        return 1;
    }

    /* Bottom row: one leaf hash per transaction. */
    for (int i = 0; i < count; i++)
        merkle_leaf_hash(&txs[i], cur[i]);

    n = count;

    /* Collapse levels until a single hash remains. */
    while (n > 1) {
        int out_n = 0;

        for (int i = 0; i < n; i += 2) {
            /* On an odd count the last node is paired with itself. */
            const char *left = cur[i];
            const char *right = (i + 1 < n) ? cur[i + 1] : cur[i];

            char concat[2 * HASH_HEX_LEN];
            size_t left_len = strlen(left);
            size_t right_len = strlen(right);

            memcpy(concat, left, left_len);
            memcpy(concat + left_len, right, right_len);

            sha256d_hex(concat, left_len + right_len, next[out_n]);
            out_n++;
        }

        /* The freshly computed row becomes the input for the next round. */
        {
            char (*tmp)[HASH_HEX_LEN] = cur;
            cur = next;
            next = tmp;
        }
        n = out_n;
    }

    memcpy(out_root, cur[0], HASH_HEX_LEN);
    return 1;
}
