#include "fraud.h"
#include "account.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int fraud_tx_is_system_minted(const Transaction *tx)
{
    return tx->transaction_type == TX_COINBASE ||
           tx->transaction_type == TX_REINSURANCE_CONTRIBUTION;
}

static int fraud_tx_moves_value(const Transaction *tx)
{
    switch (tx->transaction_type) {
    case TX_POLICY_ENROLLMENT:
    case TX_PREMIUM_PAYMENT:
    case TX_POLICY_RENEWAL:
    case TX_CLAIM_SETTLEMENT:
    case TX_TOKEN_TRANSFER:
    case TX_REINSURANCE_CONTRIBUTION:
    case TX_COINBASE:
        return 1;
    default:
        return 0;
    }
}

static void fraud_set_reason(char *reason, int reason_len, const char *msg)
{
    if (reason != NULL && reason_len > 0)
        snprintf(reason, (size_t)reason_len, "%s", msg);
}

static int fraud_count_tx_id(const ChainState *chain, const char *tx_id)
{
    int seen = 0;

    for (int b = 0; b < chain->block_count; b++) {
        const Block *block = &chain->blocks[b];
        for (int t = 0; t < block->transaction_count; t++) {
            if (strcmp(block->transactions[t].transaction_id, tx_id) == 0)
                seen++;
        }
    }

    for (int i = 0; i < chain->mempool.count; i++) {
        if (strcmp(chain->mempool.entries[i].tx.transaction_id, tx_id) == 0)
            seen++;
    }

    return seen;
}

static int fraud_tx_id_on_chain(const ChainState *chain, const char *tx_id)
{
    for (int b = 0; b < chain->block_count; b++) {
        const Block *block = &chain->blocks[b];
        for (int t = 0; t < block->transaction_count; t++) {
            if (strcmp(block->transactions[t].transaction_id, tx_id) == 0)
                return 1;
        }
    }
    return 0;
}

static int fraud_tx_uses_ref(const Transaction *tx)
{
    return tx->transaction_type == TX_CLAIM_APPROVAL ||
           tx->transaction_type == TX_CLAIM_SETTLEMENT;
}

unsigned int fraud_check_tx(const ChainState *chain, const Transaction *tx,
                            char *reason, int reason_len)
{
    unsigned int flags = FRAUD_NONE;
    const Account *sender;

    fraud_set_reason(reason, reason_len, "clean");

    if (chain == NULL || tx == NULL) {
        fraud_set_reason(reason, reason_len, "null input");
        return FRAUD_NONE;
    }
    if (fraud_tx_is_system_minted(tx))
        return FRAUD_NONE;

    if (tx->sender_address[0] != '\0' &&
        strcmp(tx->sender_address, tx->receiver_address) == 0) {
        flags |= FRAUD_SELF_TRANSFER;
        fraud_set_reason(reason, reason_len, "self-transfer");
    }
    if (tx->amount < 0.0 || isnan(tx->amount) || isinf(tx->amount)) {
        flags |= FRAUD_BAD_AMOUNT;
        fraud_set_reason(reason, reason_len, "bad amount");
    }
    if (fraud_count_tx_id(chain, tx->transaction_id) > 1) {
        flags |= FRAUD_DUPLICATE_ID;
        fraud_set_reason(reason, reason_len, "duplicate transaction id");
    }

    sender = account_find_const(chain, tx->sender_address);
    if (sender == NULL) {
        flags |= FRAUD_NONCE_ANOMALY;
        fraud_set_reason(reason, reason_len, "sender account not found");
    } else {
        if (tx->sender_nonce != sender->nonce + 1) {
            flags |= FRAUD_NONCE_ANOMALY;
            fraud_set_reason(reason, reason_len, "nonce anomaly");
        }
        if (fraud_tx_moves_value(tx) && tx->amount > sender->balance) {
            flags |= FRAUD_OVERSPEND;
            fraud_set_reason(reason, reason_len, "overspend");
        }
    }

    if (fraud_tx_moves_value(tx) && tx->amount >= FRAUD_LARGE_TRANSFER_THRESHOLD) {
        flags |= FRAUD_LARGE_TRANSFER;
        fraud_set_reason(reason, reason_len, "large transfer");
    }

    if (fraud_tx_uses_ref(tx)) {
        if (tx->ref_transaction_id[0] == '\0' ||
            !fraud_tx_id_on_chain(chain, tx->ref_transaction_id)) {
            flags |= FRAUD_DANGLING_REF;
            fraud_set_reason(reason, reason_len, "dangling reference");
        }
    }

    if (flags == FRAUD_NONE)
        fraud_set_reason(reason, reason_len, "clean");

    return flags;
}

int fraud_tx_is_suspicious(const ChainState *chain, const Transaction *tx)
{
    return fraud_check_tx(chain, tx, NULL, 0) != FRAUD_NONE;
}

int fraud_scan_mempool(ChainState *chain)
{
    int flagged = 0;

    if (chain == NULL) return 0;
    if (chain->mempool.count < 0 || chain->mempool.count > MAX_MEMPOOL)
        return 0;

    for (int i = 0; i < chain->mempool.count; i++) {
        MempoolEntry *entry = &chain->mempool.entries[i];
        if (entry->status != TX_STATUS_PENDING) continue;
        if (fraud_check_tx(chain, &entry->tx, NULL, 0) != FRAUD_NONE) {
            entry->status = TX_STATUS_SUSPICIOUS;
            flagged++;
        }
    }

    return flagged;
}
