/*
 * types.h - Central data structures, enums, and constants for the
 * AHT (ALU Health Token) Blockchain Health Insurance Management System.
 *
 * Everything that more than one module needs to agree on lives here so
 * the layout of on-disk and in-memory data is defined in exactly one place.
 */
#ifndef AHT_TYPES_H
#define AHT_TYPES_H

#include <stdint.h>
#include <time.h>

/* ----------------------------------------------------------------------- */
/* Sizes and limits                                                        */
/* ----------------------------------------------------------------------- */

#define HASH_LEN          32          /* raw SHA-256 digest in bytes        */
#define HASH_HEX_LEN      65          /* 64 hex chars + null terminator     */
#define ADDRESS_LEN       41          /* 40 hex chars (20 bytes) + null     */
#define SIG_HEX_LEN       256         /* DER-encoded ECDSA sig as hex + null */
#define PUBKEY_HEX_LEN    142         /* uncompressed EC point hex + null   */

#define MAX_TX_PER_BLOCK  128
#define MAX_BLOCKS        8192
#define MAX_MEMPOOL       2048
#define MAX_UTXOS         8192
#define MAX_ACCOUNTS      16
#define MAX_POLICIES      256

#define POLICY_DURATION_DAYS   365
#define SECONDS_PER_DAY        86400L

/* Economic constants (all amounts are whole AHT tokens). */
#define REINSURANCE_RATE          0.05    /* 5% of premium -> reinsurance   */
#define SETTLEMENT_SPLIT_THRESHOLD 1000.0 /* claims above this are split    */

/* ----------------------------------------------------------------------- */
/* Enumerations                                                            */
/* ----------------------------------------------------------------------- */

/*
 * The nine domain transaction types required by the specification, plus two
 * system-generated types (reinsurance contribution and the coinbase mining
 * reward) that the engine creates automatically.
 */
typedef enum {
    TX_POLICY_ENROLLMENT = 0,     /* 1. enrol a member, sets expiry +365d   */
    TX_PREMIUM_PAYMENT,           /* 2. pay premium, spawns 5% reinsurance  */
    TX_POLICY_RENEWAL,            /* 3. policy expiry / renewal             */
    TX_HEALTHCARE_SERVICE,        /* 4. healthcare service request          */
    TX_PRE_AUTHORIZATION,         /* 5. pre-authorisation                   */
    TX_CLAIM_SUBMISSION,          /* 6. claim submission                    */
    TX_CLAIM_APPROVAL,            /* 7. claim approval / rejection          */
    TX_CLAIM_SETTLEMENT,          /* 8. claim settlement (>1000 -> split)   */
    TX_TOKEN_TRANSFER,            /* 9. plain token transfer                */
    TX_REINSURANCE_CONTRIBUTION,  /* auto: 5% premium to reinsurance pool   */
    TX_COINBASE,                  /* auto: block reward to the miner        */
    TX_TYPE_COUNT
} TransactionType;

/* Lifecycle status of a transaction as it moves through the mempool. */
typedef enum {
    TX_STATUS_PENDING = 0,
    TX_STATUS_CONFIRMED,
    TX_STATUS_SUSPICIOUS,
    TX_STATUS_REJECTED
} TxStatus;

/* Which fixed system wallet an account represents. */
typedef enum {
    ACC_MEMBER = 0,
    ACC_PROVIDER,
    ACC_INSURANCE_POOL,
    ACC_MINER,
    ACC_REINSURANCE_POOL,
    ACC_ROLE_COUNT
} AccountRole;

/* Mining strategy selected by the operator. */
typedef enum {
    MINE_SOLO = 0,
    MINE_POOL
} MiningMode;

typedef enum {
    POLICY_STATUS_NONE = 0,
    POLICY_STATUS_ACTIVE,
    POLICY_STATUS_EXPIRED,
    POLICY_STATUS_RENEWED
} PolicyStatus;

/* ----------------------------------------------------------------------- */
/* Transaction                                                             */
/* ----------------------------------------------------------------------- */

/*
 * A single signed transaction. Note: per the spec a transaction never stores
 * its own hash. Its identity (transaction_id) is derived deterministically
 * from its signable fields, and the digital_signature covers those fields.
 */
typedef struct {
    char            transaction_id[HASH_HEX_LEN];
    char            sender_address[ADDRESS_LEN];
    char            receiver_address[ADDRESS_LEN];
    char            related_member_address[ADDRESS_LEN];
    double          amount;
    TransactionType transaction_type;
    time_t          timestamp;
    uint64_t        sender_nonce;          /* replay protection (account)   */
    char            digital_signature[SIG_HEX_LEN];

    /* Domain metadata used by specific transaction types. */
    time_t          policy_expiry;         /* enrollment / renewal          */
    char            ref_transaction_id[HASH_HEX_LEN]; /* claim/settlement link */
} Transaction;

/* ----------------------------------------------------------------------- */
/* Mempool                                                                 */
/* ----------------------------------------------------------------------- */

/*
 * A queued transaction awaiting inclusion in a block. Ordered by fee
 * (descending) then timestamp (ascending) when blocks are assembled.
 */
typedef struct {
    Transaction tx;
    double      fee;
    TxStatus    status;
} MempoolEntry;

typedef struct {
    MempoolEntry entries[MAX_MEMPOOL];
    int          count;
} Mempool;

/* ----------------------------------------------------------------------- */
/* Block                                                                   */
/* ----------------------------------------------------------------------- */

typedef struct {
    uint64_t    block_id;
    time_t      timestamp;
    int         transaction_count;
    char        previous_hash[HASH_HEX_LEN];
    char        merkle_root[HASH_HEX_LEN];
    uint64_t    nonce;                     /* proof-of-work nonce           */
    char        miner_id[ADDRESS_LEN];
    int         difficulty;                /* required leading zero hex nibbles */

    Transaction transactions[MAX_TX_PER_BLOCK];
    char        block_hash[HASH_HEX_LEN];  /* computed, cached for linking  */
} Block;

/* ----------------------------------------------------------------------- */
/* Token                                                                   */
/* ----------------------------------------------------------------------- */

typedef struct {
    char     token_name[32];
    char     token_symbol[8];
    double   total_supply;
} Token;

/* ----------------------------------------------------------------------- */
/* Account-Balance model                                                   */
/* ----------------------------------------------------------------------- */

typedef struct {
    AccountRole role;
    char        address[ADDRESS_LEN];
    char        public_key[PUBKEY_HEX_LEN];
    double      balance;
    uint64_t    nonce;                     /* last confirmed nonce          */
    char        label[32];
} Account;

/* ----------------------------------------------------------------------- */
/* UTXO model                                                              */
/* ----------------------------------------------------------------------- */

typedef struct {
    char     tx_id[HASH_HEX_LEN];          /* originating transaction id    */
    int      output_index;
    char     owner_address[ADDRESS_LEN];
    double   amount;
    int      spent;                        /* 0 = unspent, 1 = spent        */
} UTXO;

typedef struct {
    UTXO utxos[MAX_UTXOS];
    int  count;
} UTXOSet;

/* ----------------------------------------------------------------------- */
/* Policy registry                                                         */
/* ----------------------------------------------------------------------- */

typedef struct {
    char         policy_id[32];
    char         member_address[ADDRESS_LEN];
    char         coverage_plan[32];
    time_t       enrollment_date;
    time_t       expiry_date;
    PolicyStatus status;
} Policy;

/* ----------------------------------------------------------------------- */
/* Global chain state                                                      */
/* ----------------------------------------------------------------------- */

/*
 * Aggregate runtime state for the whole system. A single instance of this is
 * created at startup, mutated by the CLI/engine, and serialised to disk.
 */
typedef struct {
    Block    blocks[MAX_BLOCKS];
    int      block_count;

    Mempool  mempool;
    UTXOSet  utxos;

    Account  accounts[MAX_ACCOUNTS];
    int      account_count;

    Policy   policies[MAX_POLICIES];
    int      policy_count;

    Token    token;

    int      difficulty;                   /* current PoW difficulty        */
    int      last_retarget_block;          /* height at last retarget       */
    double   block_reward;                 /* configurable miner reward     */

    /* Pools tracked for insurance economics / auditing. */
    double   insurance_pool_balance;
    double   reinsurance_pool_balance;
} ChainState;

#endif /* AHT_TYPES_H */
