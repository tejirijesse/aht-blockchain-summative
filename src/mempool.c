/*
 * mempool.c - Pending-transaction pool for the AHT blockchain.
 *
 * Storage model: a flat, fixed-capacity array of MempoolEntry records inside
 * Mempool (entries[0..count-1]). Transactions are keyed by transaction_id, so
 * the pool refuses a resubmission of an id it already holds.
 *
 * Prioritisation for block assembly is fee-descending, then (on a fee tie)
 * timestamp-ascending - the standard "pay more to go first, older breaks ties"
 * ordering. It is offered two ways: an in-place sort (mempool_sort) and a
 * non-destructive selection of the top-N still-PENDING transactions
 * (mempool_select_for_block).
 */
#include "mempool.h"

#include <string.h>

void mempool_init(Mempool *mp)
{
    if (mp == NULL) return;
    mp->count = 0;
}

int mempool_size(const Mempool *mp)
{
    if (mp == NULL) return 0;
    return mp->count;
}

int mempool_find(const Mempool *mp, const char *transaction_id)
{
    if (mp == NULL || transaction_id == NULL) return -1;

    for (int i = 0; i < mp->count; i++) {
        if (strcmp(mp->entries[i].tx.transaction_id, transaction_id) == 0)
            return i;
    }
    return -1;
}

int mempool_add(Mempool *mp, const Transaction *tx, double fee)
{
    if (mp == NULL || tx == NULL) return 0;
    if (fee < 0.0) return 0;
    if (mp->count >= MAX_MEMPOOL) return 0;

    /* Reject duplicates: a transaction_id may appear at most once. */
    if (mempool_find(mp, tx->transaction_id) != -1) return 0;

    MempoolEntry *e = &mp->entries[mp->count];
    e->tx = *tx;
    e->fee = fee;
    e->status = TX_STATUS_PENDING;
    mp->count++;
    return 1;
}

int mempool_set_status(Mempool *mp, const char *transaction_id, TxStatus status)
{
    int idx;

    if (mp == NULL || transaction_id == NULL) return 0;

    idx = mempool_find(mp, transaction_id);
    if (idx < 0) return 0;

    mp->entries[idx].status = status;
    return 1;
}

int mempool_remove_at(Mempool *mp, int index)
{
    if (mp == NULL) return 0;
    if (index < 0 || index >= mp->count) return 0;

    /* Stable removal: shift every later entry down one slot. */
    for (int i = index; i < mp->count - 1; i++)
        mp->entries[i] = mp->entries[i + 1];

    mp->count--;
    return 1;
}

int mempool_purge_finalized(Mempool *mp)
{
    int removed = 0;
    int i = 0;

    if (mp == NULL) return 0;

    /*
     * Single-pass compaction: copy survivors forward, count the rest. Walking
     * with two indices keeps it stable and O(n) (no repeated shifting).
     */
    while (i < mp->count) {
        TxStatus s = mp->entries[i].status;
        if (s == TX_STATUS_CONFIRMED || s == TX_STATUS_REJECTED) {
            removed++;
            i++;
        } else {
            if (removed > 0)
                mp->entries[i - removed] = mp->entries[i];
            i++;
        }
    }

    mp->count -= removed;
    return removed;
}

/*
 * Priority comparison between two entries. Returns < 0 if `a` should come
 * before `b` (higher priority), > 0 if after, 0 if equal priority.
 * Order: higher fee first; on equal fee, older timestamp first.
 */
static int entry_higher_priority(const MempoolEntry *a, const MempoolEntry *b)
{
    if (a->fee > b->fee) return -1;
    if (a->fee < b->fee) return 1;
    if (a->tx.timestamp < b->tx.timestamp) return -1;
    if (a->tx.timestamp > b->tx.timestamp) return 1;
    return 0;
}

void mempool_sort(Mempool *mp)
{
    if (mp == NULL) return;

    /*
     * Insertion sort: stable and simple, and the pool is small enough
     * (MAX_MEMPOOL) that its O(n^2) worst case is irrelevant here. Stability
     * preserves insertion order among entries that are equal on both keys.
     */
    for (int i = 1; i < mp->count; i++) {
        MempoolEntry key = mp->entries[i];
        int j = i - 1;

        while (j >= 0 && entry_higher_priority(&mp->entries[j], &key) > 0) {
            mp->entries[j + 1] = mp->entries[j];
            j--;
        }
        mp->entries[j + 1] = key;
    }
}

int mempool_select_for_block(const Mempool *mp, Transaction *out, int max)
{
    Mempool tmp;
    int n = 0;

    if (mp == NULL || out == NULL || max <= 0) return 0;

    /*
     * Work on a copy so the live pool's order is never disturbed. Only the
     * PENDING entries are eligible candidates; finalised or suspicious ones
     * are excluded before sorting so the ranking reflects only what can ship.
     */
    tmp.count = 0;
    for (int i = 0; i < mp->count; i++) {
        if (mp->entries[i].status == TX_STATUS_PENDING)
            tmp.entries[tmp.count++] = mp->entries[i];
    }

    mempool_sort(&tmp);

    for (int i = 0; i < tmp.count && n < max; i++)
        out[n++] = tmp.entries[i].tx;

    return n;
}
