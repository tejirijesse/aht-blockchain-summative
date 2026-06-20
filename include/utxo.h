#ifndef AHT_UTXO_H
#define AHT_UTXO_H

#include "types.h"

void utxo_set_init(UTXOSet *set);
UTXO *utxo_add(UTXOSet *set, const char *tx_id, int output_index,
               const char *owner_address, double amount);
UTXO *utxo_find(UTXOSet *set, const char *tx_id, int output_index);
const UTXO *utxo_find_const(const UTXOSet *set, const char *tx_id,
                            int output_index);
int utxo_mark_spent(UTXOSet *set, const char *tx_id, int output_index);
double utxo_balance(const UTXOSet *set, const char *address);
int utxo_select_inputs(const UTXOSet *set, const char *address, double amount,
                       int *out_indices, int out_cap, double *out_total);
int utxo_apply_tx(UTXOSet *set, const Transaction *tx);
int utxo_apply_block(UTXOSet *set, const Block *block);

#endif
