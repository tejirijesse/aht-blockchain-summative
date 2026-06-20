#ifndef AHT_FRAUD_H
#define AHT_FRAUD_H

#include "types.h"

typedef enum {
    FRAUD_NONE           = 0,
    FRAUD_SELF_TRANSFER  = 1 << 0,
    FRAUD_BAD_AMOUNT     = 1 << 1,
    FRAUD_NONCE_ANOMALY  = 1 << 2,
    FRAUD_DUPLICATE_ID   = 1 << 3,
    FRAUD_OVERSPEND      = 1 << 4,
    FRAUD_LARGE_TRANSFER = 1 << 5,
    FRAUD_DANGLING_REF   = 1 << 6
} FraudFlag;

#define FRAUD_LARGE_TRANSFER_THRESHOLD 50000.0

unsigned int fraud_check_tx(const ChainState *chain, const Transaction *tx,
                            char *reason, int reason_len);
int fraud_tx_is_suspicious(const ChainState *chain, const Transaction *tx);
int fraud_scan_mempool(ChainState *chain);

#endif
