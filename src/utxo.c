#include "utxo.h"

#include <stdio.h>
#include <string.h>

void utxo_set_init(UTXOSet *set)
{
    if (set == NULL) return;
    set->count = 0;
}

static int utxo_tx_is_system_minted(const Transaction *tx)
{
    return tx->transaction_type == TX_COINBASE ||
           tx->transaction_type == TX_REINSURANCE_CONTRIBUTION;
}

static int utxo_tx_moves_value(const Transaction *tx)
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

UTXO *utxo_add(UTXOSet *set, const char *tx_id, int output_index,
               const char *owner_address, double amount)
{
    UTXO *out;

    if (set == NULL || tx_id == NULL || owner_address == NULL) return NULL;
    if (output_index < 0 || amount < 0.0 || set->count >= MAX_UTXOS) return NULL;

    out = &set->utxos[set->count];
    memset(out, 0, sizeof(*out));
    snprintf(out->tx_id, sizeof(out->tx_id), "%s", tx_id);
    out->output_index = output_index;
    snprintf(out->owner_address, sizeof(out->owner_address), "%s", owner_address);
    out->amount = amount;
    out->spent = 0;
    set->count++;
    return out;
}

UTXO *utxo_find(UTXOSet *set, const char *tx_id, int output_index)
{
    if (set == NULL || tx_id == NULL) return NULL;
    for (int i = 0; i < set->count; i++) {
        if (set->utxos[i].output_index == output_index &&
            strcmp(set->utxos[i].tx_id, tx_id) == 0)
            return &set->utxos[i];
    }
    return NULL;
}

const UTXO *utxo_find_const(const UTXOSet *set, const char *tx_id, int output_index)
{
    if (set == NULL || tx_id == NULL) return NULL;
    for (int i = 0; i < set->count; i++) {
        if (set->utxos[i].output_index == output_index &&
            strcmp(set->utxos[i].tx_id, tx_id) == 0)
            return &set->utxos[i];
    }
    return NULL;
}

int utxo_mark_spent(UTXOSet *set, const char *tx_id, int output_index)
{
    UTXO *out = utxo_find(set, tx_id, output_index);
    if (out == NULL || out->spent) return 0;
    out->spent = 1;
    return 1;
}

double utxo_balance(const UTXOSet *set, const char *address)
{
    double total = 0.0;

    if (set == NULL || address == NULL) return 0.0;
    for (int i = 0; i < set->count; i++) {
        if (!set->utxos[i].spent &&
            strcmp(set->utxos[i].owner_address, address) == 0)
            total += set->utxos[i].amount;
    }
    return total;
}

int utxo_select_inputs(const UTXOSet *set, const char *address, double amount,
                       int *out_indices, int out_cap, double *out_total)
{
    double total = 0.0;
    int selected = 0;

    if (set == NULL || address == NULL || out_indices == NULL || amount < 0.0)
        return -1;

    for (int i = 0; i < set->count; i++) {
        if (set->utxos[i].spent) continue;
        if (strcmp(set->utxos[i].owner_address, address) != 0) continue;
        if (selected >= out_cap) return -1;
        out_indices[selected++] = i;
        total += set->utxos[i].amount;
        if (total >= amount) {
            if (out_total != NULL) *out_total = total;
            return selected;
        }
    }

    return -1;
}

int utxo_apply_tx(UTXOSet *set, const Transaction *tx)
{
    int indices[MAX_UTXOS];
    double total = 0.0;
    int selected;
    double change;

    if (set == NULL || tx == NULL || tx->amount < 0.0) return 0;

    if (utxo_tx_is_system_minted(tx)) {
        if (tx->receiver_address[0] == '\0') return 0;
        return utxo_add(set, tx->transaction_id, 0, tx->receiver_address, tx->amount) != NULL;
    }

    if (!utxo_tx_moves_value(tx))
        return 1;

    if (tx->sender_address[0] == '\0' || tx->receiver_address[0] == '\0') return 0;

    selected = utxo_select_inputs(set, tx->sender_address, tx->amount,
                                  indices, MAX_UTXOS, &total);
    if (selected < 0) return 0;

    for (int i = 0; i < selected; i++)
        set->utxos[indices[i]].spent = 1;

    if (utxo_add(set, tx->transaction_id, 0, tx->receiver_address, tx->amount) == NULL)
        return 0;

    change = total - tx->amount;
    if (change > 0.0) {
        if (utxo_add(set, tx->transaction_id, 1, tx->sender_address, change) == NULL)
            return 0;
    }

    return 1;
}

int utxo_apply_block(UTXOSet *set, const Block *block)
{
    int applied = 0;

    if (set == NULL || block == NULL) return 0;
    if (block->transaction_count < 0 || block->transaction_count > MAX_TX_PER_BLOCK)
        return 0;

    for (int i = 0; i < block->transaction_count; i++) {
        if (utxo_apply_tx(set, &block->transactions[i]))
            applied++;
    }

    return applied;
}
