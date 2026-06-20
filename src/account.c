#include "account.h"
#include "crypto.h"
#include "transaction.h"

#include <stdio.h>
#include <string.h>

static const char *account_role_label(AccountRole role)
{
    switch (role) {
    case ACC_MEMBER: return "member";
    case ACC_PROVIDER: return "provider";
    case ACC_INSURANCE_POOL: return "insurance_pool";
    case ACC_MINER: return "miner";
    case ACC_REINSURANCE_POOL: return "reinsurance_pool";
    default: return "unknown";
    }
}

static double account_role_genesis_balance(AccountRole role)
{
    switch (role) {
    case ACC_MEMBER: return ACCOUNT_GENESIS_MEMBER_BALANCE;
    case ACC_PROVIDER: return ACCOUNT_GENESIS_PROVIDER_BALANCE;
    case ACC_INSURANCE_POOL: return ACCOUNT_GENESIS_INSURANCE_POOL_BALANCE;
    case ACC_MINER: return ACCOUNT_GENESIS_MINER_BALANCE;
    case ACC_REINSURANCE_POOL: return ACCOUNT_GENESIS_REINSURANCE_BALANCE;
    default: return 0.0;
    }
}

static void account_role_seed(AccountRole role, char *out_seed, size_t out_seed_len)
{
    snprintf(out_seed, out_seed_len, "aht-system-wallet|%d|%s",
             (int)role, account_role_label(role));
}

int account_seed_system_wallets(ChainState *chain)
{
    int created = 0;

    if (chain == NULL) return -1;
    if (ACC_ROLE_COUNT > MAX_ACCOUNTS) return -1;

    chain->account_count = 0;

    for (int role = 0; role < ACC_ROLE_COUNT; role++) {
        Account *acct = &chain->accounts[chain->account_count];
        KeyPair wallet;
        char seed[64];
        memset(acct, 0, sizeof(*acct));
        acct->role = (AccountRole)role;
        account_role_seed((AccountRole)role, seed, sizeof(seed));
        if (!crypto_generate_keypair_from_seed(seed, &wallet)) return -1;
        snprintf(acct->address, sizeof(acct->address), "%s", wallet.address);
        snprintf(acct->public_key, sizeof(acct->public_key), "%s", wallet.public_key);
        acct->balance = account_role_genesis_balance((AccountRole)role);
        acct->nonce = 0;
        snprintf(acct->label, sizeof(acct->label), "%s",
                 account_role_label((AccountRole)role));
        chain->account_count++;
        created++;
    }

    chain->insurance_pool_balance = ACCOUNT_GENESIS_INSURANCE_POOL_BALANCE;
    chain->reinsurance_pool_balance = ACCOUNT_GENESIS_REINSURANCE_BALANCE;
    return created;
}

Account *account_find(ChainState *chain, const char *address)
{
    if (chain == NULL || address == NULL) return NULL;
    for (int i = 0; i < chain->account_count; i++) {
        if (strcmp(chain->accounts[i].address, address) == 0)
            return &chain->accounts[i];
    }
    return NULL;
}

const Account *account_find_const(const ChainState *chain, const char *address)
{
    if (chain == NULL || address == NULL) return NULL;
    for (int i = 0; i < chain->account_count; i++) {
        if (strcmp(chain->accounts[i].address, address) == 0)
            return &chain->accounts[i];
    }
    return NULL;
}

Account *account_find_by_role(ChainState *chain, AccountRole role)
{
    if (chain == NULL) return NULL;
    for (int i = 0; i < chain->account_count; i++) {
        if (chain->accounts[i].role == role)
            return &chain->accounts[i];
    }
    return NULL;
}

double account_balance(const ChainState *chain, const char *address)
{
    const Account *acct = account_find_const(chain, address);
    return acct != NULL ? acct->balance : 0.0;
}

static int account_tx_is_system_minted(const Transaction *tx)
{
    return tx->transaction_type == TX_COINBASE ||
           tx->transaction_type == TX_REINSURANCE_CONTRIBUTION;
}

static int account_tx_moves_value(const Transaction *tx)
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

static void account_set_reason(char *reason, int reason_len, const char *msg)
{
    if (reason != NULL && reason_len > 0)
        snprintf(reason, (size_t)reason_len, "%s", msg);
}

int account_validate_tx(const ChainState *chain, const Transaction *tx,
                        char *reason, int reason_len)
{
    const Account *sender;

    if (chain == NULL || tx == NULL) {
        account_set_reason(reason, reason_len, "null input");
        return 0;
    }

    if (account_tx_is_system_minted(tx)) {
        account_set_reason(reason, reason_len, "ok (system-minted)");
        return 1;
    }

    sender = account_find_const(chain, tx->sender_address);
    if (sender == NULL) {
        account_set_reason(reason, reason_len, "sender account not found");
        return 0;
    }
    if (tx->sender_nonce != sender->nonce + 1) {
        account_set_reason(reason, reason_len, "bad nonce");
        return 0;
    }
    if (tx->amount < 0.0) {
        account_set_reason(reason, reason_len, "negative amount");
        return 0;
    }
    if (account_tx_moves_value(tx) && sender->balance < tx->amount) {
        account_set_reason(reason, reason_len, "insufficient balance");
        return 0;
    }

    account_set_reason(reason, reason_len, "ok");
    return 1;
}

int account_verify_tx_signature(const ChainState *chain, const Transaction *tx,
                                char *reason, int reason_len)
{
    const Account *sender;

    if (chain == NULL || tx == NULL) {
        account_set_reason(reason, reason_len, "null input");
        return 0;
    }
    if (account_tx_is_system_minted(tx)) {
        account_set_reason(reason, reason_len, "ok (system-minted)");
        return 1;
    }

    sender = account_find_const(chain, tx->sender_address);
    if (sender == NULL) {
        account_set_reason(reason, reason_len, "sender account not found");
        return 0;
    }
    if (sender->public_key[0] == '\0') {
        account_set_reason(reason, reason_len, "sender public key missing");
        return 0;
    }
    if (tx->digital_signature[0] == '\0') {
        account_set_reason(reason, reason_len, "missing digital signature");
        return 0;
    }
    if (!tx_verify_signature(tx, sender->public_key)) {
        account_set_reason(reason, reason_len, "signature verification failed");
        return 0;
    }

    account_set_reason(reason, reason_len, "ok");
    return 1;
}

static Account *account_find_or_create(ChainState *chain, const char *address,
                                       AccountRole role)
{
    Account *acct = account_find(chain, address);
    if (acct != NULL) return acct;
    if (chain->account_count >= MAX_ACCOUNTS) return NULL;

    acct = &chain->accounts[chain->account_count];
    memset(acct, 0, sizeof(*acct));
    acct->role = role;
    snprintf(acct->address, sizeof(acct->address), "%s", address);
    snprintf(acct->label, sizeof(acct->label), "%s", account_role_label(role));
    chain->account_count++;
    return acct;
}

int account_apply_tx(ChainState *chain, const Transaction *tx)
{
    Account *sender;
    Account *receiver;
    int system_minted;

    if (chain == NULL || tx == NULL) return 0;
    if (!account_validate_tx(chain, tx, NULL, 0)) return 0;

    system_minted = account_tx_is_system_minted(tx);

    if (account_tx_moves_value(tx) && tx->receiver_address[0] != '\0') {
        receiver = account_find_or_create(chain, tx->receiver_address, ACC_MEMBER);
        if (receiver == NULL) return 0;
        receiver->balance += tx->amount;
    }

    if (!system_minted) {
        sender = account_find(chain, tx->sender_address);
        if (sender == NULL) return 0;
        if (account_tx_moves_value(tx))
            sender->balance -= tx->amount;
        sender->nonce = tx->sender_nonce;
    }

    if (tx->transaction_type == TX_PREMIUM_PAYMENT) {
        chain->insurance_pool_balance += tx->amount;
    } else if (tx->transaction_type == TX_REINSURANCE_CONTRIBUTION) {
        chain->insurance_pool_balance -= tx->amount;
        chain->reinsurance_pool_balance += tx->amount;
    } else if (tx->transaction_type == TX_CLAIM_SETTLEMENT) {
        Account *pool = account_find(chain, tx->sender_address);
        if (pool != NULL && pool->role == ACC_INSURANCE_POOL)
            chain->insurance_pool_balance -= tx->amount;
        if (pool != NULL && pool->role == ACC_REINSURANCE_POOL)
            chain->reinsurance_pool_balance -= tx->amount;
    }

    return 1;
}

int account_apply_block(ChainState *chain, const Block *block)
{
    int applied = 0;

    if (chain == NULL || block == NULL) return 0;
    if (block->transaction_count < 0 || block->transaction_count > MAX_TX_PER_BLOCK)
        return 0;

    for (int i = 0; i < block->transaction_count; i++) {
        if (account_apply_tx(chain, &block->transactions[i]))
            applied++;
    }

    return applied;
}
