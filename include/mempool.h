/*
 * mempool.h - Pending-transaction pool for the AHT blockchain.
 *
 * The mempool holds validated-but-unconfirmed transactions between the moment
 * they are submitted and the moment a miner includes them in a block. It is a
 * fixed-capacity store (MAX_MEMPOOL entries) of MempoolEntry records, each
 * pairing a Transaction with its fee and lifecycle status.
 *
 * Prioritisation: when a block is assembled the highest-fee transactions are
 * taken first; ties are broken by the older timestamp. This is the standard
 * fee-descending, then timestamp-ascending ordering, exposed both as an
 * in-place sort and as a selection routine that copies the top-N transactions
 * for a block without disturbing the pool.
 *
 * Identity / de-duplication: a transaction is keyed by its transaction_id, so
 * resubmitting the same id is rejected. Capacity and null arguments are checked
 * on every entry point.
 */
#ifndef AHT_MEMPOOL_H
#define AHT_MEMPOOL_H

#include "types.h"

/* Reset the pool to empty (count = 0). Safe to call on a fresh or used pool. */
void mempool_init(Mempool *mp);

/*
 * Add a transaction with the given fee. The entry starts PENDING. Returns:
 *   1  on success,
 *   0  on bad input (null pool/tx), a negative fee, a duplicate
 *      transaction_id, or when the pool is already at MAX_MEMPOOL.
 */
int mempool_add(Mempool *mp, const Transaction *tx, double fee);

/* Number of entries currently queued (0 on a null pool). */
int mempool_size(const Mempool *mp);

/*
 * Locate an entry by transaction_id. Returns its index in entries[], or -1 if
 * not present (or on bad input).
 */
int mempool_find(const Mempool *mp, const char *transaction_id);

/*
 * Update the status of the entry with the given transaction_id. Returns 1 if an
 * entry was found and updated, 0 otherwise.
 */
int mempool_set_status(Mempool *mp, const char *transaction_id, TxStatus status);

/*
 * Remove the entry at `index`, preserving the relative order of the remaining
 * entries (stable removal). Returns 1 on success, 0 if the index is out range.
 */
int mempool_remove_at(Mempool *mp, int index);

/*
 * Remove every entry whose status is CONFIRMED or REJECTED (i.e. those that no
 * longer belong in the pending pool). Returns the number of entries removed.
 */
int mempool_purge_finalized(Mempool *mp);

/*
 * Sort the pool in place by descending fee, then ascending timestamp. After
 * this call entries[0] is the most attractive transaction for a miner.
 */
void mempool_sort(Mempool *mp);

/*
 * Select up to `max` transactions for a new block, in priority order
 * (fee-descending, timestamp-ascending), copying them into `out`. Only entries
 * that are still PENDING are eligible; the pool itself is left unchanged.
 * Returns the number of transactions written to `out`.
 */
int mempool_select_for_block(const Mempool *mp, Transaction *out, int max);

#endif /* AHT_MEMPOOL_H */
