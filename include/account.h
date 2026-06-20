#ifndef AHT_ACCOUNT_H
#define AHT_ACCOUNT_H

#include "types.h"

#define ACCOUNT_GENESIS_MEMBER_BALANCE          10000.0
#define ACCOUNT_GENESIS_PROVIDER_BALANCE        5000.0
#define ACCOUNT_GENESIS_INSURANCE_POOL_BALANCE  100000.0
#define ACCOUNT_GENESIS_MINER_BALANCE           0.0
#define ACCOUNT_GENESIS_REINSURANCE_BALANCE     0.0

int account_seed_system_wallets(ChainState *chain);
Account *account_find(ChainState *chain, const char *address);
const Account *account_find_const(const ChainState *chain, const char *address);
Account *account_find_by_role(ChainState *chain, AccountRole role);
double account_balance(const ChainState *chain, const char *address);
int account_validate_tx(const ChainState *chain, const Transaction *tx,
                        char *reason, int reason_len);
int account_verify_tx_signature(const ChainState *chain, const Transaction *tx,
                                char *reason, int reason_len);
int account_apply_tx(ChainState *chain, const Transaction *tx);
int account_apply_block(ChainState *chain, const Block *block);

#endif
