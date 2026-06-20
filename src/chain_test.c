#include "account.h"
#include "block.h"
#include "blockchain.h"
#include "crypto.h"
#include "fraud.h"
#include "insurance.h"
#include "mempool.h"
#include "mining.h"
#include "transaction.h"
#include "utxo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static ChainState g_chain;

typedef struct {
    char address[ADDRESS_LEN];
    char public_key[PUBKEY_HEX_LEN];
    char private_key[PRIVKEY_HEX_LEN];
    int has_public_key;
    int has_private_key;
} WalletMaterial;

static int parse_int_arg(const char *text, int *out)
{
    char *end;
    long value;

    if (text == NULL || out == NULL) return 0;
    value = strtol(text, &end, 10);
    if (*text == '\0' || *end != '\0') return 0;
    if (value < 0 || value > 2147483647L) return 0;
    *out = (int)value;
    return 1;
}

static int parse_double_arg(const char *text, double *out)
{
    char *end;
    double value;

    if (text == NULL || out == NULL) return 0;
    value = strtod(text, &end);
    if (*text == '\0' || *end != '\0') return 0;
    if (value < 0.0) return 0;
    *out = value;
    return 1;
}

static int read_wallet_file(const char *path, WalletMaterial *wallet)
{
    FILE *fp;
    char line[512];

    if (path == NULL || wallet == NULL) return 0;
    fp = fopen(path, "r");
    if (fp == NULL) return 0;

    memset(wallet, 0, sizeof(*wallet));
    while (fgets(line, sizeof(line), fp) != NULL) {
        char *value = strchr(line, '=');
        if (value == NULL) continue;
        *value++ = '\0';
        value[strcspn(value, "\r\n")] = '\0';

        if (strcmp(line, "address") == 0) {
            snprintf(wallet->address, sizeof(wallet->address), "%s", value);
        } else if (strcmp(line, "public_key") == 0) {
            snprintf(wallet->public_key, sizeof(wallet->public_key), "%s", value);
            wallet->has_public_key = 1;
        } else if (strcmp(line, "private_key") == 0) {
            snprintf(wallet->private_key, sizeof(wallet->private_key), "%s", value);
            wallet->has_private_key = 1;
        }
    }

    fclose(fp);
    return wallet->address[0] != '\0';
}

static const char *account_role_name(AccountRole role)
{
    switch (role) {
    case ACC_MEMBER: return "member";
    case ACC_PROVIDER: return "provider";
    case ACC_INSURANCE_POOL: return "insurance-pool";
    case ACC_MINER: return "miner";
    case ACC_REINSURANCE_POOL: return "reinsurance-pool";
    default: return "unknown";
    }
}

static const char *account_role_seed_name(AccountRole role)
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

static int parse_account_role(const char *text, AccountRole *out)
{
    if (text == NULL || out == NULL) return 0;
    if (strcmp(text, "member") == 0) { *out = ACC_MEMBER; return 1; }
    if (strcmp(text, "provider") == 0) { *out = ACC_PROVIDER; return 1; }
    if (strcmp(text, "insurance-pool") == 0) { *out = ACC_INSURANCE_POOL; return 1; }
    if (strcmp(text, "miner") == 0) { *out = ACC_MINER; return 1; }
    if (strcmp(text, "reinsurance-pool") == 0) { *out = ACC_REINSURANCE_POOL; return 1; }
    return 0;
}

static int write_wallet_file(const char *path, const KeyPair *wallet)
{
    FILE *fp;

    if (path == NULL || wallet == NULL || path[0] == '\0') return 0;
    fp = fopen(path, "w");
    if (fp == NULL) return 0;

    if (fprintf(fp, "address=%s\npublic_key=%s\nprivate_key=%s\n",
                wallet->address, wallet->public_key, wallet->private_key) < 0) {
        fclose(fp);
        return 0;
    }
    return fclose(fp) == 0;
}

static void print_usage(const char *argv0)
{
    printf("AHT Blockchain CLI\n\n");
    printf("Usage:\n");
    printf("  %s demo\n", argv0);
    printf("  %s init <chain-file> <miner-id> [difficulty] [reward]\n", argv0);
    printf("  %s verify <chain-file>\n", argv0);
    printf("  %s status <chain-file>\n", argv0);
    printf("  %s wallet-create <wallet-file>\n", argv0);
    printf("  %s account-add <chain-file> <role> <label> <address-or-wallet-file> [balance]\n", argv0);
    printf("  %s account-list <chain-file>\n", argv0);
    printf("  %s balance <chain-file> <address>\n", argv0);
    printf("  %s policy-enroll <chain-file> <policy-id> <member-wallet-file> <coverage-plan> [premium]\n", argv0);
    printf("  %s policy-renew <chain-file> <policy-id> <member-wallet-file> [premium]\n", argv0);
    printf("  %s policy-list <chain-file>\n", argv0);
    printf("  %s premium-pay <chain-file> <member-wallet-file> <amount>\n", argv0);
    printf("  %s claim-submit <chain-file> <policy-id> <provider-wallet-file> <amount> <service-ref>\n", argv0);
    printf("  %s claim-approve <chain-file> <provider-address> <amount> <claim-id-or-txid>\n", argv0);
    printf("  %s claim-settle <chain-file> <provider-address> <amount> <approval-id-or-txid>\n", argv0);
    printf("  %s mempool-list <chain-file>\n", argv0);
    printf("  %s fraud-scan <chain-file>\n", argv0);
    printf("  %s mine-pending <chain-file> <miner-id>\n", argv0);
    printf("  %s reinsurance-balance <chain-file>\n", argv0);
    printf("  %s mine-empty <chain-file> <miner-id>\n", argv0);
}

static const Account *role_account(const ChainState *chain, AccountRole role)
{
    return account_find_by_role((ChainState *)chain, role);
}

static int load_sender_wallet(const char *path, WalletMaterial *wallet)
{
    if (!read_wallet_file(path, wallet)) {
        fprintf(stderr, "failed to read wallet file: %s\n", path);
        return 0;
    }
    if (!wallet->has_public_key || !wallet->has_private_key) {
        fprintf(stderr, "wallet file missing public/private key: %s\n", path);
        return 0;
    }
    return 1;
}

static int system_wallet_for_role(AccountRole role, WalletMaterial *wallet)
{
    char seed[64];
    KeyPair keypair;

    if (wallet == NULL) return 0;
    memset(wallet, 0, sizeof(*wallet));
    snprintf(seed, sizeof(seed), "aht-system-wallet|%d|%s",
             (int)role, account_role_seed_name(role));
    if (!crypto_generate_keypair_from_seed(seed, &keypair)) return 0;
    snprintf(wallet->address, sizeof(wallet->address), "%s", keypair.address);
    snprintf(wallet->public_key, sizeof(wallet->public_key), "%s", keypair.public_key);
    snprintf(wallet->private_key, sizeof(wallet->private_key), "%s", keypair.private_key);
    wallet->has_public_key = 1;
    wallet->has_private_key = 1;
    return 1;
}

static uint64_t next_nonce(const ChainState *chain, const char *address)
{
    const Account *account = account_find_const(chain, address);
    return account != NULL ? account->nonce + 1 : 1;
}

static int find_chain_transaction_id_by_sequence(const ChainState *chain,
                                                 TransactionType type,
                                                 int sequence_number,
                                                 char *out_tx_id,
                                                 size_t out_tx_id_len)
{
    int seen = 0;

    if (chain == NULL || out_tx_id == NULL || out_tx_id_len == 0) return 0;
    if (sequence_number <= 0) return 0;

    for (int b = 0; b < chain->block_count; b++) {
        const Block *block = &chain->blocks[b];

        for (int t = 0; t < block->transaction_count; t++) {
            const Transaction *tx = &block->transactions[t];

            if (tx->transaction_type != type) continue;
            seen++;
            if (seen == sequence_number) {
                snprintf(out_tx_id, out_tx_id_len, "%s", tx->transaction_id);
                return 1;
            }
        }
    }

    return 0;
}

static int resolve_transaction_reference(const ChainState *chain,
                                         const char *reference,
                                         TransactionType type,
                                         const char *prefix,
                                         char *out_tx_id,
                                         size_t out_tx_id_len)
{
    const char *digits;
    char *end = NULL;
    long sequence_number;

    if (chain == NULL || reference == NULL || out_tx_id == NULL || out_tx_id_len == 0)
        return 0;

    if (strlen(reference) == HASH_HEX_LEN - 1) {
        snprintf(out_tx_id, out_tx_id_len, "%s", reference);
        return 1;
    }

    if (prefix == NULL) return 0;
    if (strncmp(reference, prefix, strlen(prefix)) != 0) return 0;

    digits = reference + strlen(prefix);
    sequence_number = strtol(digits, &end, 10);
    if (digits[0] == '\0' || end == NULL || *end != '\0' || sequence_number <= 0)
        return 0;

    return find_chain_transaction_id_by_sequence(chain, type, (int)sequence_number,
                                                 out_tx_id, out_tx_id_len);
}

static void seed_account_utxos(ChainState *chain)
{
    char tx_id[HASH_HEX_LEN];

    utxo_set_init(&chain->utxos);
    for (int i = 0; i < chain->account_count; i++) {
        const Account *account = &chain->accounts[i];
        if (account->balance <= 0.0) continue;
        snprintf(tx_id, sizeof(tx_id), "seed-%s", account->address);
        utxo_add(&chain->utxos, tx_id, 0, account->address, account->balance);
    }
}

static void add_funding_utxo(ChainState *chain, const char *address, double balance)
{
    char tx_id[HASH_HEX_LEN];

    if (balance <= 0.0) return;
    snprintf(tx_id, sizeof(tx_id), "manual-%s-%d", address, chain->utxos.count);
    utxo_add(&chain->utxos, tx_id, 0, address, balance);
}

static void print_status(const ChainState *chain)
{
    const Block *tip;

    if (chain == NULL) return;

    tip = blockchain_tip(chain);
    printf("block_count: %d\n", chain->block_count);
    printf("difficulty: %d\n", chain->difficulty);
    printf("block_reward: %.2f\n", chain->block_reward);
    printf("total_supply: %.2f %s\n", chain->token.total_supply, chain->token.token_symbol);
    printf("mempool_count: %d\n", chain->mempool.count);
    printf("account_count: %d\n", chain->account_count);
    printf("policy_count: %d\n", chain->policy_count);
    printf("utxo_count: %d\n", chain->utxos.count);
    printf("insurance_pool_balance: %.2f\n", chain->insurance_pool_balance);
    printf("reinsurance_pool_balance: %.2f\n", chain->reinsurance_pool_balance);

    if (tip != NULL) {
        printf("tip_height: %llu\n", (unsigned long long)tip->block_id);
        printf("tip_hash: %s\n", tip->block_hash);
        printf("tip_timestamp: %lld\n", (long long)tip->timestamp);
    }
}

static int queue_tx_with_screening(ChainState *chain, const Transaction *tx, double fee,
                                   const char *label, int flag_if_suspicious)
{
    char reason[128];
    unsigned int fraud_flags;

    if (!account_validate_tx(chain, tx, reason, (int)sizeof(reason))) {
        fprintf(stderr, "%s rejected: %s\n", label, reason);
        return 0;
    }
    if (!account_verify_tx_signature(chain, tx, reason, (int)sizeof(reason))) {
        fprintf(stderr, "%s rejected: %s\n", label, reason);
        return 0;
    }
    if (!mempool_add(&chain->mempool, tx, fee)) {
        fprintf(stderr, "%s rejected: mempool add failed\n", label);
        return 0;
    }

    fraud_flags = fraud_check_tx(chain, tx, reason, (int)sizeof(reason));
    if (fraud_flags != FRAUD_NONE || flag_if_suspicious) {
        mempool_set_status(&chain->mempool, tx->transaction_id, TX_STATUS_SUSPICIOUS);
        printf("%s queued as suspicious: %s (%s)\n", label, tx->transaction_id, reason);
    } else {
        printf("%s queued: %s\n", label, tx->transaction_id);
    }

    return 1;
}

static void mark_block_transactions_confirmed(ChainState *chain, const Block *block)
{
    for (int i = 0; i < block->transaction_count; i++)
        mempool_set_status(&chain->mempool, block->transactions[i].transaction_id,
                           TX_STATUS_CONFIRMED);
}

static int run_demo(void)
{
    ChainState *chain = &g_chain;
    ChainState *loaded;
    static Block next_block;
    int ok;

    loaded = malloc(sizeof(*loaded));
    if (loaded == NULL) {
        fprintf(stderr, "failed to allocate demo chain buffer\n");
        return 1;
    }

    ok = blockchain_init(chain, "miner-addr", 1, 50.0);
    account_seed_system_wallets(chain);
    seed_account_utxos(chain);
    printf("init: %d (block_count=%d)\n", ok, chain->block_count);
    printf("genesis hash: %s\n", chain->blocks[0].block_hash);
    printf("genesis prev: %s\n", chain->blocks[0].previous_hash);

    ok = blockchain_make_next_block(chain, &next_block, NULL, 0, "miner-addr", 1000);
    printf("make_next: %d (block_id=%llu)\n", ok, (unsigned long long)next_block.block_id);
    ok = mining_proof_of_work(&next_block, chain->difficulty, 0, &(MiningResult){0});
    printf("mine: %d (nonce=%llu, hash=%s)\n", ok,
           (unsigned long long)next_block.nonce, next_block.block_hash);
    ok = blockchain_append_block(chain, &next_block);
    printf("append: %d (block_count=%d)\n", ok, chain->block_count);

    ok = blockchain_make_next_block(chain, &next_block, NULL, 0, "miner-addr", 2000);
    ok = mining_proof_of_work(&next_block, chain->difficulty, 0, &(MiningResult){0});
    ok = blockchain_append_block(chain, &next_block);
    printf("append2: %d (block_count=%d)\n", ok, chain->block_count);

    printf("verify (clean): %d\n", blockchain_verify(chain));
    ok = blockchain_save(chain, "build/aht.chain");
    printf("save: %d\n", ok);
    ok = blockchain_load(loaded, "build/aht.chain");
    printf("load: %d (block_count=%d)\n", ok, loaded->block_count);
    printf("verify (reloaded): %d\n", blockchain_verify(loaded));
    strcpy(chain->blocks[1].previous_hash, GENESIS_PREV_HASH);
    printf("verify (tampered prev_hash): %d\n", blockchain_verify(chain));

    free(loaded);
    return 0;
}

static int run_init(const char *path, const char *miner_id,
                    const char *difficulty_text, const char *reward_text)
{
    ChainState *chain = &g_chain;
    int difficulty = 1;
    double reward = 50.0;

    if (difficulty_text != NULL && !parse_int_arg(difficulty_text, &difficulty)) {
        fprintf(stderr, "invalid difficulty: %s\n", difficulty_text);
        return 1;
    }
    if (reward_text != NULL && !parse_double_arg(reward_text, &reward)) {
        fprintf(stderr, "invalid reward: %s\n", reward_text);
        return 1;
    }
    if (!blockchain_init(chain, miner_id, difficulty, reward)) {
        fprintf(stderr, "failed to initialise chain\n");
        return 1;
    }
    if (account_seed_system_wallets(chain) < 0) {
        fprintf(stderr, "failed to seed system wallets\n");
        return 1;
    }
    seed_account_utxos(chain);

    if (!blockchain_save(chain, path)) {
        fprintf(stderr, "failed to save chain: %s\n", path);
        return 1;
    }

    printf("initialised chain at %s\n", path);
    print_status(chain);
    return 0;
}

static int run_verify(const char *path)
{
    if (!blockchain_load(&g_chain, path)) {
        fprintf(stderr, "failed to load or verify chain: %s\n", path);
        return 1;
    }
    printf("verify: ok\n");
    return 0;
}

static int run_status(const char *path)
{
    if (!blockchain_load(&g_chain, path)) {
        fprintf(stderr, "failed to load chain: %s\n", path);
        return 1;
    }
    print_status(&g_chain);
    return 0;
}

static int run_wallet_create(const char *path)
{
    KeyPair wallet;

    if (!crypto_generate_keypair(&wallet)) {
        fprintf(stderr, "failed to generate wallet\n");
        return 1;
    }
    if (!write_wallet_file(path, &wallet)) {
        fprintf(stderr, "failed to write wallet file: %s\n", path);
        return 1;
    }

    printf("wallet created at %s\n", path);
    printf("address: %s\n", wallet.address);
    printf("public_key: %s\n", wallet.public_key);
    printf("private_key: %s\n", wallet.private_key);
    return 0;
}

static int run_account_add(const char *path, const char *role_text,
                           const char *label, const char *address_or_wallet,
                           const char *balance_text)
{
    AccountRole role;
    Account *account;
    double balance = 0.0;
    WalletMaterial wallet;
    const char *address = address_or_wallet;
    int has_wallet = 0;

    if (!parse_account_role(role_text, &role)) {
        fprintf(stderr, "invalid role: %s\n", role_text);
        return 1;
    }
    if (strlen(label) >= sizeof(g_chain.accounts[0].label)) {
        fprintf(stderr, "label too long\n");
        return 1;
    }
    if (read_wallet_file(address_or_wallet, &wallet)) {
        address = wallet.address;
        has_wallet = 1;
    } else if (strlen(address) != ADDRESS_LEN - 1) {
        fprintf(stderr, "invalid address or wallet file: %s\n", address_or_wallet);
        return 1;
    }
    if (balance_text != NULL && !parse_double_arg(balance_text, &balance)) {
        fprintf(stderr, "invalid balance: %s\n", balance_text);
        return 1;
    }
    if (!blockchain_load(&g_chain, path)) {
        fprintf(stderr, "failed to load chain: %s\n", path);
        return 1;
    }
    if (g_chain.account_count >= MAX_ACCOUNTS) {
        fprintf(stderr, "account table is full\n");
        return 1;
    }
    if (account_find(&g_chain, address) != NULL) {
        fprintf(stderr, "account already exists for address: %s\n", address);
        return 1;
    }

    account = &g_chain.accounts[g_chain.account_count];
    memset(account, 0, sizeof(*account));
    account->role = role;
    account->balance = balance;
    account->nonce = 0;
    strncpy(account->label, label, sizeof(account->label) - 1);
    strncpy(account->address, address, sizeof(account->address) - 1);
    if (has_wallet && wallet.has_public_key)
        strncpy(account->public_key, wallet.public_key, sizeof(account->public_key) - 1);
    g_chain.account_count++;
    add_funding_utxo(&g_chain, address, balance);

    if (!blockchain_save(&g_chain, path)) {
        fprintf(stderr, "failed to save chain: %s\n", path);
        return 1;
    }

    printf("account added to %s\n", path);
    printf("role: %s\n", account_role_name(account->role));
    printf("label: %s\n", account->label);
    printf("address: %s\n", account->address);
    printf("balance: %.2f\n", account->balance);
    return 0;
}

static int run_account_list(const char *path)
{
    if (!blockchain_load(&g_chain, path)) {
        fprintf(stderr, "failed to load chain: %s\n", path);
        return 1;
    }

    printf("accounts: %d\n", g_chain.account_count);
    for (int i = 0; i < g_chain.account_count; i++) {
        const Account *account = &g_chain.accounts[i];
        printf("[%d] %s | %s | %s | balance=%.2f | nonce=%llu\n",
               i, account_role_name(account->role), account->label,
               account->address, account->balance,
               (unsigned long long)account->nonce);
        if (account->public_key[0] != '\0')
            printf("    public_key=%s\n", account->public_key);
    }
    return 0;
}

static int run_balance(const char *path, const char *address)
{
    if (!blockchain_load(&g_chain, path)) {
        fprintf(stderr, "failed to load chain: %s\n", path);
        return 1;
    }
    printf("account balance: %.2f\n", account_balance(&g_chain, address));
    printf("utxo balance: %.2f\n", utxo_balance(&g_chain.utxos, address));
    return 0;
}

static int run_policy_enroll(const char *path, const char *policy_id,
                             const char *member_wallet_file, const char *coverage_plan,
                             const char *premium_text)
{
    const Account *pool;
    Transaction tx;
    double premium = 0.0;
    WalletMaterial member_wallet;
    time_t now = time(NULL);

    if (premium_text != NULL && !parse_double_arg(premium_text, &premium)) {
        fprintf(stderr, "invalid premium: %s\n", premium_text);
        return 1;
    }
    if (!load_sender_wallet(member_wallet_file, &member_wallet)) return 1;
    if (!blockchain_load(&g_chain, path)) {
        fprintf(stderr, "failed to load chain: %s\n", path);
        return 1;
    }
    if (account_find(&g_chain, member_wallet.address) == NULL) {
        fprintf(stderr, "member account not found: %s\n", member_wallet.address);
        return 1;
    }
    pool = role_account(&g_chain, ACC_INSURANCE_POOL);
    if (pool == NULL) {
        fprintf(stderr, "insurance pool wallet missing\n");
        return 1;
    }
    if (!insurance_enroll_policy(&g_chain, policy_id, member_wallet.address, coverage_plan, now)) {
        fprintf(stderr, "failed to enroll policy\n");
        return 1;
    }
    if (!tx_make_policy_enrollment(&tx, member_wallet.address, pool->address,
                                   premium, next_nonce(&g_chain, member_wallet.address), now)) {
        fprintf(stderr, "failed to build enrollment transaction\n");
        return 1;
    }
    if (!tx_sign(&tx, member_wallet.private_key)) {
        fprintf(stderr, "failed to sign enrollment transaction\n");
        return 1;
    }
    if (!queue_tx_with_screening(&g_chain, &tx, 1.0, "policy enrollment", 0))
        return 1;
    if (!blockchain_save(&g_chain, path)) return 1;

    printf("policy enrolled: %s (%s)\n", policy_id, coverage_plan);
    return 0;
}

static int run_policy_renew(const char *path, const char *policy_id,
                            const char *member_wallet_file, const char *premium_text)
{
    const Policy *policy;
    const Account *pool;
    Transaction tx;
    double premium = 0.0;
    WalletMaterial member_wallet;
    time_t now = time(NULL);

    if (premium_text != NULL && !parse_double_arg(premium_text, &premium)) {
        fprintf(stderr, "invalid premium: %s\n", premium_text);
        return 1;
    }
    if (!load_sender_wallet(member_wallet_file, &member_wallet)) return 1;
    if (!blockchain_load(&g_chain, path)) {
        fprintf(stderr, "failed to load chain: %s\n", path);
        return 1;
    }
    policy = insurance_find_policy_const_by_id(&g_chain, policy_id);
    pool = role_account(&g_chain, ACC_INSURANCE_POOL);
    if (policy == NULL || pool == NULL) {
        fprintf(stderr, "policy or insurance pool missing\n");
        return 1;
    }
    if (!insurance_renew_policy(&g_chain, policy_id, now)) {
        fprintf(stderr, "failed to renew policy\n");
        return 1;
    }
    policy = insurance_find_policy_const_by_id(&g_chain, policy_id);
    if (!tx_make_policy_renewal(&tx, policy->member_address, pool->address,
                                premium, next_nonce(&g_chain, policy->member_address), now)) {
        fprintf(stderr, "failed to build renewal transaction\n");
        return 1;
    }
    if (strcmp(policy->member_address, member_wallet.address) != 0) {
        fprintf(stderr, "wallet does not match policy member\n");
        return 1;
    }
    if (!tx_sign(&tx, member_wallet.private_key)) {
        fprintf(stderr, "failed to sign renewal transaction\n");
        return 1;
    }
    if (!queue_tx_with_screening(&g_chain, &tx, 1.0, "policy renewal", 0))
        return 1;
    if (!blockchain_save(&g_chain, path)) return 1;
    printf("policy renewed: %s\n", policy_id);
    return 0;
}

static int run_policy_list(const char *path)
{
    if (!blockchain_load(&g_chain, path)) {
        fprintf(stderr, "failed to load chain: %s\n", path);
        return 1;
    }

    printf("policies: %d\n", g_chain.policy_count);
    for (int i = 0; i < g_chain.policy_count; i++) {
        Policy *policy = &g_chain.policies[i];
        insurance_refresh_policy_status(policy, time(NULL));
        printf("[%d] %s | member=%s | plan=%s | status=%s | expiry=%lld\n",
               i, policy->policy_id, policy->member_address, policy->coverage_plan,
               insurance_policy_status_name(policy->status),
               (long long)policy->expiry_date);
    }
    return 0;
}

static int run_premium_pay(const char *path, const char *member_wallet_file, const char *amount_text)
{
    const Account *pool;
    const Account *reinsurance;
    Transaction premium_tx;
    Transaction reinsurance_tx;
    double amount;
    WalletMaterial member_wallet;
    WalletMaterial pool_wallet;
    time_t now = time(NULL);

    if (!parse_double_arg(amount_text, &amount)) {
        fprintf(stderr, "invalid amount: %s\n", amount_text);
        return 1;
    }
    if (!load_sender_wallet(member_wallet_file, &member_wallet)) return 1;
    if (!blockchain_load(&g_chain, path)) {
        fprintf(stderr, "failed to load chain: %s\n", path);
        return 1;
    }
    pool = role_account(&g_chain, ACC_INSURANCE_POOL);
    reinsurance = role_account(&g_chain, ACC_REINSURANCE_POOL);
    if (pool == NULL || reinsurance == NULL) {
        fprintf(stderr, "system wallets missing\n");
        return 1;
    }

    if (!tx_make_premium_payment(&premium_tx, member_wallet.address, pool->address,
                                 amount, next_nonce(&g_chain, member_wallet.address), now)) {
        fprintf(stderr, "failed to build premium transaction\n");
        return 1;
    }
    if (!tx_sign(&premium_tx, member_wallet.private_key)) {
        fprintf(stderr, "failed to sign premium transaction\n");
        return 1;
    }
    if (!queue_tx_with_screening(&g_chain, &premium_tx, 1.0, "premium payment", 0))
        return 1;

    if (!system_wallet_for_role(ACC_INSURANCE_POOL, &pool_wallet)) {
        fprintf(stderr, "failed to derive insurance pool wallet\n");
        return 1;
    }
    if (!tx_make_reinsurance_contribution(&reinsurance_tx, pool->address, reinsurance->address,
                                          amount, next_nonce(&g_chain, pool->address), now)) {
        fprintf(stderr, "failed to build reinsurance transaction\n");
        return 1;
    }
    if (!tx_sign(&reinsurance_tx, pool_wallet.private_key)) {
        fprintf(stderr, "failed to sign reinsurance transaction\n");
        return 1;
    }
    if (!queue_tx_with_screening(&g_chain, &reinsurance_tx, 1.0,
                                 "reinsurance contribution", 0))
        return 1;

    if (!blockchain_save(&g_chain, path)) return 1;
    return 0;
}

static int run_claim_submit(const char *path, const char *policy_id,
                            const char *provider_wallet_file, const char *amount_text,
                            const char *service_ref)
{
    const Policy *policy;
    const Account *pool;
    Transaction tx;
    double amount;
    char reason[128];
    WalletMaterial provider_wallet;
    time_t now = time(NULL);

    if (!parse_double_arg(amount_text, &amount)) {
        fprintf(stderr, "invalid amount: %s\n", amount_text);
        return 1;
    }
    if (!load_sender_wallet(provider_wallet_file, &provider_wallet)) return 1;
    if (!blockchain_load(&g_chain, path)) {
        fprintf(stderr, "failed to load chain: %s\n", path);
        return 1;
    }
    if (!insurance_claim_allowed(&g_chain, policy_id, now, reason, (int)sizeof(reason))) {
        fprintf(stderr, "claim rejected: %s\n", reason);
        if (!blockchain_save(&g_chain, path)) return 1;
        return 1;
    }

    policy = insurance_find_policy_const_by_id(&g_chain, policy_id);
    pool = role_account(&g_chain, ACC_INSURANCE_POOL);
    if (policy == NULL || pool == NULL) {
        fprintf(stderr, "policy or pool missing\n");
        return 1;
    }

    if (!tx_make_claim_submission(&tx, provider_wallet.address, pool->address, amount,
                                  service_ref, policy->expiry_date,
                                  next_nonce(&g_chain, provider_wallet.address), now)) {
        fprintf(stderr, "failed to build claim transaction\n");
        return 1;
    }
    if (!tx_sign(&tx, provider_wallet.private_key)) {
        fprintf(stderr, "failed to sign claim transaction\n");
        return 1;
    }
    if (!queue_tx_with_screening(&g_chain, &tx, 1.0, "claim submission", 0))
        return 1;
    if (!blockchain_save(&g_chain, path)) return 1;
    return 0;
}

static int run_claim_approve(const char *path, const char *provider_address,
                             const char *amount_text, const char *claim_id)
{
    const Account *pool;
    char claim_tx_id[HASH_HEX_LEN];
    Transaction tx;
    double amount;
    WalletMaterial pool_wallet;
    time_t now = time(NULL);

    if (!parse_double_arg(amount_text, &amount)) {
        fprintf(stderr, "invalid amount: %s\n", amount_text);
        return 1;
    }
    if (!blockchain_load(&g_chain, path)) {
        fprintf(stderr, "failed to load chain: %s\n", path);
        return 1;
    }
    pool = role_account(&g_chain, ACC_INSURANCE_POOL);
    if (pool == NULL) {
        fprintf(stderr, "insurance pool wallet missing\n");
        return 1;
    }
    if (!resolve_transaction_reference(&g_chain, claim_id, TX_CLAIM_SUBMISSION, "CLM-",
                                       claim_tx_id, sizeof(claim_tx_id))) {
        fprintf(stderr, "claim reference not found on chain: %s\n", claim_id);
        return 1;
    }
    if (!tx_make_claim_approval(&tx, pool->address, provider_address, amount,
                                claim_tx_id, next_nonce(&g_chain, pool->address), now)) {
        fprintf(stderr, "failed to build approval transaction\n");
        return 1;
    }
    if (!system_wallet_for_role(ACC_INSURANCE_POOL, &pool_wallet)) {
        fprintf(stderr, "failed to derive insurance pool wallet\n");
        return 1;
    }
    if (!tx_sign(&tx, pool_wallet.private_key)) {
        fprintf(stderr, "failed to sign approval transaction\n");
        return 1;
    }
    if (!queue_tx_with_screening(&g_chain, &tx, 1.0, "claim approval", 0))
        return 1;
    if (!blockchain_save(&g_chain, path)) return 1;
    return 0;
}

static int run_claim_settle(const char *path, const char *provider_address,
                            const char *amount_text, const char *approval_id)
{
    const Account *pool;
    const Account *reinsurance;
    char approval_tx_id[HASH_HEX_LEN];
    Transaction pool_tx;
    Transaction reinsurance_tx;
    double amount;
    double pool_amount;
    double excess;
    double payable_excess;
    int shortfall = 0;
    WalletMaterial pool_wallet;
    WalletMaterial reinsurance_wallet;
    time_t now = time(NULL);

    if (!parse_double_arg(amount_text, &amount)) {
        fprintf(stderr, "invalid amount: %s\n", amount_text);
        return 1;
    }
    if (!blockchain_load(&g_chain, path)) {
        fprintf(stderr, "failed to load chain: %s\n", path);
        return 1;
    }
    pool = role_account(&g_chain, ACC_INSURANCE_POOL);
    reinsurance = role_account(&g_chain, ACC_REINSURANCE_POOL);
    if (pool == NULL || reinsurance == NULL) {
        fprintf(stderr, "system wallets missing\n");
        return 1;
    }
    if (!resolve_transaction_reference(&g_chain, approval_id, TX_CLAIM_APPROVAL, "APR-",
                                       approval_tx_id, sizeof(approval_tx_id))) {
        fprintf(stderr, "approval reference not found on chain: %s\n", approval_id);
        return 1;
    }

    pool_amount = amount > SETTLEMENT_SPLIT_THRESHOLD ? SETTLEMENT_SPLIT_THRESHOLD : amount;
    excess = amount - pool_amount;

    if (!tx_make_claim_settlement(&pool_tx, pool->address, provider_address, pool_amount,
                                  approval_tx_id, next_nonce(&g_chain, pool->address), now)) {
        fprintf(stderr, "failed to build pool settlement transaction\n");
        return 1;
    }
    if (!system_wallet_for_role(ACC_INSURANCE_POOL, &pool_wallet) ||
        !tx_sign(&pool_tx, pool_wallet.private_key)) {
        fprintf(stderr, "failed to sign pool settlement transaction\n");
        return 1;
    }
    if (!queue_tx_with_screening(&g_chain, &pool_tx, 1.0, "claim settlement", 0))
        return 1;

    if (excess > 0.0) {
        payable_excess = excess;
        if (g_chain.reinsurance_pool_balance < excess) {
            payable_excess = g_chain.reinsurance_pool_balance;
            shortfall = 1;
        }

        if (payable_excess > 0.0) {
            if (!tx_make_claim_settlement(&reinsurance_tx, reinsurance->address,
                                          provider_address, payable_excess, approval_tx_id,
                                          next_nonce(&g_chain, reinsurance->address), now)) {
                fprintf(stderr, "failed to build reinsurance settlement transaction\n");
                return 1;
            }
            if (!system_wallet_for_role(ACC_REINSURANCE_POOL, &reinsurance_wallet) ||
                !tx_sign(&reinsurance_tx, reinsurance_wallet.private_key)) {
                fprintf(stderr, "failed to sign reinsurance settlement transaction\n");
                return 1;
            }
            if (!queue_tx_with_screening(&g_chain, &reinsurance_tx, 1.0,
                                         "reinsurance settlement", shortfall))
                return 1;
        }
    }

    if (!blockchain_save(&g_chain, path)) return 1;

    if (shortfall) {
        printf("manual review required: reinsurance pool could only cover %.2f of %.2f excess\n",
               g_chain.reinsurance_pool_balance, excess);
    }
    return 0;
}

static int run_mempool_list(const char *path)
{
    if (!blockchain_load(&g_chain, path)) {
        fprintf(stderr, "failed to load chain: %s\n", path);
        return 1;
    }

    printf("mempool entries: %d\n", g_chain.mempool.count);
    for (int i = 0; i < g_chain.mempool.count; i++) {
        const MempoolEntry *entry = &g_chain.mempool.entries[i];
        printf("[%d] %s | fee=%.2f | status=%d | amount=%.2f | id=%s\n",
               i, tx_type_name(entry->tx.transaction_type), entry->fee,
               entry->status, entry->tx.amount, entry->tx.transaction_id);
    }
    return 0;
}

static int run_fraud_scan(const char *path)
{
    int flagged;

    if (!blockchain_load(&g_chain, path)) {
        fprintf(stderr, "failed to load chain: %s\n", path);
        return 1;
    }
    flagged = fraud_scan_mempool(&g_chain);
    if (!blockchain_save(&g_chain, path)) return 1;
    printf("fraud scan flagged %d entr%s\n", flagged, flagged == 1 ? "y" : "ies");
    return 0;
}

static int run_mine_pending(const char *path, const char *miner_id)
{
    Transaction selected[MAX_TX_PER_BLOCK];
    MiningResult result;
    const Block *tip;
    int count;
    time_t now;

    if (!blockchain_load(&g_chain, path)) {
        fprintf(stderr, "failed to load chain: %s\n", path);
        return 1;
    }

    count = mempool_select_for_block(&g_chain.mempool, selected, MAX_TX_PER_BLOCK - 1);
    now = time(NULL);
    if (!mining_mine_block(&g_chain, selected, count, miner_id, MINE_SOLO, now, &result)) {
        fprintf(stderr, "failed to mine block\n");
        return 1;
    }

    tip = blockchain_tip(&g_chain);
    if (tip != NULL) {
        mark_block_transactions_confirmed(&g_chain, tip);
        account_apply_block(&g_chain, tip);
        utxo_apply_block(&g_chain.utxos, tip);
    }
    mempool_purge_finalized(&g_chain.mempool);
    mining_retarget_difficulty(&g_chain);

    if (!blockchain_save(&g_chain, path)) return 1;

    printf("mined block at height %d with %d selected txs\n", g_chain.block_count - 1, count);
    printf("nonce: %llu\n", (unsigned long long)result.nonce);
    printf("attempts: %llu\n", (unsigned long long)result.attempts);
    printf("hash: %s\n", result.block_hash);
    print_status(&g_chain);
    return 0;
}

static int run_reinsurance_balance(const char *path)
{
    if (!blockchain_load(&g_chain, path)) {
        fprintf(stderr, "failed to load chain: %s\n", path);
        return 1;
    }
    printf("reinsurance_pool_balance: %.2f\n", g_chain.reinsurance_pool_balance);
    return 0;
}

static int run_mine_empty(const char *path, const char *miner_id)
{
    MiningResult result;
    time_t now;

    if (!blockchain_load(&g_chain, path)) {
        fprintf(stderr, "failed to load chain: %s\n", path);
        return 1;
    }
    now = time(NULL);
    if (!mining_mine_block(&g_chain, NULL, 0, miner_id, MINE_SOLO, now, &result)) {
        fprintf(stderr, "failed to mine block\n");
        return 1;
    }
    mark_block_transactions_confirmed(&g_chain, blockchain_tip(&g_chain));
    account_apply_block(&g_chain, blockchain_tip(&g_chain));
    utxo_apply_block(&g_chain.utxos, blockchain_tip(&g_chain));
    mining_retarget_difficulty(&g_chain);
    if (!blockchain_save(&g_chain, path)) return 1;

    printf("mined empty block at height %d\n", g_chain.block_count - 1);
    printf("hash: %s\n", result.block_hash);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "demo") == 0) return run_demo();
    if (strcmp(argv[1], "init") == 0) {
        if (argc < 4 || argc > 6) return print_usage(argv[0]), 1;
        return run_init(argv[2], argv[3], argc >= 5 ? argv[4] : NULL,
                        argc >= 6 ? argv[5] : NULL);
    }
    if (strcmp(argv[1], "verify") == 0) return argc == 3 ? run_verify(argv[2]) : (print_usage(argv[0]), 1);
    if (strcmp(argv[1], "status") == 0) return argc == 3 ? run_status(argv[2]) : (print_usage(argv[0]), 1);
    if (strcmp(argv[1], "wallet-create") == 0) return argc == 3 ? run_wallet_create(argv[2]) : (print_usage(argv[0]), 1);
    if (strcmp(argv[1], "account-add") == 0) return (argc >= 6 && argc <= 7) ? run_account_add(argv[2], argv[3], argv[4], argv[5], argc == 7 ? argv[6] : NULL) : (print_usage(argv[0]), 1);
    if (strcmp(argv[1], "account-list") == 0) return argc == 3 ? run_account_list(argv[2]) : (print_usage(argv[0]), 1);
    if (strcmp(argv[1], "balance") == 0) return argc == 4 ? run_balance(argv[2], argv[3]) : (print_usage(argv[0]), 1);
    if (strcmp(argv[1], "policy-enroll") == 0) return (argc >= 6 && argc <= 7) ? run_policy_enroll(argv[2], argv[3], argv[4], argv[5], argc == 7 ? argv[6] : NULL) : (print_usage(argv[0]), 1);
    if (strcmp(argv[1], "policy-renew") == 0) return (argc >= 5 && argc <= 6) ? run_policy_renew(argv[2], argv[3], argv[4], argc == 6 ? argv[5] : NULL) : (print_usage(argv[0]), 1);
    if (strcmp(argv[1], "policy-list") == 0) return argc == 3 ? run_policy_list(argv[2]) : (print_usage(argv[0]), 1);
    if (strcmp(argv[1], "premium-pay") == 0) return argc == 5 ? run_premium_pay(argv[2], argv[3], argv[4]) : (print_usage(argv[0]), 1);
    if (strcmp(argv[1], "claim-submit") == 0) return argc == 7 ? run_claim_submit(argv[2], argv[3], argv[4], argv[5], argv[6]) : (print_usage(argv[0]), 1);
    if (strcmp(argv[1], "claim-approve") == 0) return argc == 6 ? run_claim_approve(argv[2], argv[3], argv[4], argv[5]) : (print_usage(argv[0]), 1);
    if (strcmp(argv[1], "claim-settle") == 0) return argc == 6 ? run_claim_settle(argv[2], argv[3], argv[4], argv[5]) : (print_usage(argv[0]), 1);
    if (strcmp(argv[1], "mempool-list") == 0) return argc == 3 ? run_mempool_list(argv[2]) : (print_usage(argv[0]), 1);
    if (strcmp(argv[1], "fraud-scan") == 0) return argc == 3 ? run_fraud_scan(argv[2]) : (print_usage(argv[0]), 1);
    if (strcmp(argv[1], "mine-pending") == 0) return argc == 4 ? run_mine_pending(argv[2], argv[3]) : (print_usage(argv[0]), 1);
    if (strcmp(argv[1], "reinsurance-balance") == 0) return argc == 3 ? run_reinsurance_balance(argv[2]) : (print_usage(argv[0]), 1);
    if (strcmp(argv[1], "mine-empty") == 0) return argc == 4 ? run_mine_empty(argv[2], argv[3]) : (print_usage(argv[0]), 1);

    print_usage(argv[0]);
    return 1;
}
