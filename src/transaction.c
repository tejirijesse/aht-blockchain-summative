/*
 * transaction.c - Construction, identity, and signing for the AHT health
 * insurance transaction types.
 *
 * Identity: a transaction never stores its own hash. tx_compute_id() derives it
 * on demand as the SHA-256 of a canonical serialisation of the signable fields
 * (tx_serialize_signable). That field order matches merkle_leaf_hash but omits
 * transaction_id and digital_signature, the two fields derived from it. Any
 * change to a confirmed transaction's content therefore yields a different id
 * and a different Merkle root.
 *
 * Signing: tx_sign() signs the same signable bytes (not the id) with the
 * sender's secp256k1 private key via crypto_sign(); tx_verify_signature()
 * checks them against a public key via crypto_verify().
 */
#include "transaction.h"
#include "sha256.h"

#include <stdio.h>
#include <string.h>

/* ---- Identity and signing ----------------------------------------------- */

size_t tx_serialize_signable(const Transaction *tx, char *out, size_t out_cap)
{
    int written;

    if (tx == NULL || out == NULL || out_cap == 0) return 0;

    /*
     * Canonical signable form: same field order as merkle_leaf_hash, but
     * excluding transaction_id and digital_signature (the derived fields).
     * Fixed precision on the amount keeps the bytes deterministic across
     * platforms; explicit '|' separators prevent field-boundary collisions.
     */
    written = snprintf(out, out_cap,
                       "%s|%s|%.8f|%d|%lld|%llu|%lld|%s",
                       tx->sender_address,
                       tx->receiver_address,
                       tx->amount,
                       (int)tx->transaction_type,
                       (long long)tx->timestamp,
                       (unsigned long long)tx->sender_nonce,
                       (long long)tx->policy_expiry,
                       tx->ref_transaction_id);

    if (written < 0 || (size_t)written >= out_cap) return 0;
    return (size_t)written;
}

int tx_compute_id(Transaction *tx)
{
    char buf[1024];
    size_t len;

    if (tx == NULL) return 0;

    len = tx_serialize_signable(tx, buf, sizeof(buf));
    if (len == 0) return 0;

    sha256_hex(buf, len, tx->transaction_id);
    return 1;
}

int tx_sign(Transaction *tx, const char *private_key_hex)
{
    char buf[1024];
    size_t len;

    if (tx == NULL || private_key_hex == NULL) return 0;

    /* Sign the signable bytes themselves (crypto_sign hashes internally). */
    len = tx_serialize_signable(tx, buf, sizeof(buf));
    if (len == 0) return 0;

    return crypto_sign(private_key_hex, buf, len, tx->digital_signature);
}

int tx_verify_signature(const Transaction *tx, const char *public_key_hex)
{
    char buf[1024];
    size_t len;

    if (tx == NULL || public_key_hex == NULL) return 0;

    len = tx_serialize_signable(tx, buf, sizeof(buf));
    if (len == 0) return 0;

    return crypto_verify(public_key_hex, buf, len, tx->digital_signature);
}

/* ---- Internal helpers ---------------------------------------------------- */

/*
 * Zero `out` and fill the fields common to every transaction. Addresses are
 * copied with bounded length so an over-long argument can never overflow the
 * fixed-size struct fields. Returns 1 on success, 0 on bad input.
 */
static int tx_init_common(Transaction *out, const char *sender,
                          const char *receiver, double amount,
                          TransactionType type, uint64_t nonce, time_t now)
{
    if (out == NULL) return 0;

    memset(out, 0, sizeof(*out));

    if (sender != NULL) {
        strncpy(out->sender_address, sender, ADDRESS_LEN - 1);
        out->sender_address[ADDRESS_LEN - 1] = '\0';
    }
    if (receiver != NULL) {
        strncpy(out->receiver_address, receiver, ADDRESS_LEN - 1);
        out->receiver_address[ADDRESS_LEN - 1] = '\0';
    }

    out->amount = amount;
    out->transaction_type = type;
    out->timestamp = now;
    out->sender_nonce = nonce;
    return 1;
}

/* Copy a referenced transaction id into out->ref_transaction_id (bounded). */
static void tx_set_ref(Transaction *out, const char *ref_id)
{
    if (ref_id == NULL) return;
    strncpy(out->ref_transaction_id, ref_id, HASH_HEX_LEN - 1);
    out->ref_transaction_id[HASH_HEX_LEN - 1] = '\0';
}

/* ---- Domain rule helpers ------------------------------------------------- */

double tx_reinsurance_amount(double premium)
{
    return premium * REINSURANCE_RATE;
}

double tx_settlement_reinsurance_portion(double amount)
{
    if (amount <= SETTLEMENT_SPLIT_THRESHOLD) return 0.0;
    return amount - SETTLEMENT_SPLIT_THRESHOLD;
}

/* ---- Constructors (the nine domain types + reinsurance + coinbase) ------- */

int tx_make_policy_enrollment(Transaction *out, const char *member_addr,
                              const char *pool_addr, double premium,
                              uint64_t nonce, time_t now)
{
    if (member_addr == NULL || pool_addr == NULL) return 0;
    if (!tx_init_common(out, member_addr, pool_addr, premium,
                        TX_POLICY_ENROLLMENT, nonce, now))
        return 0;

    /* Coverage starts now and runs for POLICY_DURATION_DAYS. */
    out->policy_expiry = now + (time_t)POLICY_DURATION_DAYS * SECONDS_PER_DAY;
    return tx_compute_id(out);
}

int tx_make_premium_payment(Transaction *out, const char *member_addr,
                            const char *pool_addr, double premium,
                            uint64_t nonce, time_t now)
{
    if (member_addr == NULL || pool_addr == NULL) return 0;
    if (!tx_init_common(out, member_addr, pool_addr, premium,
                        TX_PREMIUM_PAYMENT, nonce, now))
        return 0;
    return tx_compute_id(out);
}

int tx_make_reinsurance_contribution(Transaction *out, const char *pool_addr,
                                     const char *reinsurance_addr,
                                     double premium, uint64_t nonce,
                                     time_t now)
{
    if (pool_addr == NULL || reinsurance_addr == NULL) return 0;
    if (!tx_init_common(out, pool_addr, reinsurance_addr,
                        tx_reinsurance_amount(premium),
                        TX_REINSURANCE_CONTRIBUTION, nonce, now))
        return 0;
    return tx_compute_id(out);
}

int tx_make_policy_renewal(Transaction *out, const char *member_addr,
                           const char *pool_addr, double premium,
                           uint64_t nonce, time_t now)
{
    if (member_addr == NULL || pool_addr == NULL) return 0;
    if (!tx_init_common(out, member_addr, pool_addr, premium,
                        TX_POLICY_RENEWAL, nonce, now))
        return 0;

    /* Renewal extends coverage another POLICY_DURATION_DAYS from now. */
    out->policy_expiry = now + (time_t)POLICY_DURATION_DAYS * SECONDS_PER_DAY;
    return tx_compute_id(out);
}

int tx_make_healthcare_service(Transaction *out, const char *member_addr,
                               const char *provider_addr, double amount,
                               uint64_t nonce, time_t now)
{
    if (member_addr == NULL || provider_addr == NULL) return 0;
    if (!tx_init_common(out, member_addr, provider_addr, amount,
                        TX_HEALTHCARE_SERVICE, nonce, now))
        return 0;
    return tx_compute_id(out);
}

int tx_make_pre_authorization(Transaction *out, const char *provider_addr,
                              const char *pool_addr, double amount,
                              const char *ref_service_id, uint64_t nonce,
                              time_t now)
{
    if (provider_addr == NULL || pool_addr == NULL) return 0;
    if (!tx_init_common(out, provider_addr, pool_addr, amount,
                        TX_PRE_AUTHORIZATION, nonce, now))
        return 0;
    tx_set_ref(out, ref_service_id);
    return tx_compute_id(out);
}

int tx_make_claim_submission(Transaction *out, const char *provider_addr,
                             const char *pool_addr, double amount,
                             const char *ref_service_id, time_t policy_expiry,
                             uint64_t nonce, time_t now)
{
    if (provider_addr == NULL || pool_addr == NULL) return 0;
    if (!tx_init_common(out, provider_addr, pool_addr, amount,
                        TX_CLAIM_SUBMISSION, nonce, now))
        return 0;
    tx_set_ref(out, ref_service_id);

    /* Carry the member's policy expiry so downstream logic can reject claims
     * made against an already-expired policy. */
    out->policy_expiry = policy_expiry;
    return tx_compute_id(out);
}

int tx_make_claim_approval(Transaction *out, const char *pool_addr,
                           const char *provider_addr, double amount,
                           const char *ref_claim_id, uint64_t nonce,
                           time_t now)
{
    if (pool_addr == NULL || provider_addr == NULL) return 0;
    if (!tx_init_common(out, pool_addr, provider_addr, amount,
                        TX_CLAIM_APPROVAL, nonce, now))
        return 0;
    tx_set_ref(out, ref_claim_id);
    return tx_compute_id(out);
}

int tx_make_claim_rejection(Transaction *out, const char *pool_addr,
                            const char *provider_addr,
                            const char *ref_claim_id, uint64_t nonce,
                            time_t now)
{
    if (pool_addr == NULL || provider_addr == NULL) return 0;
    /*
     * A rejection is an acknowledgement that moves no funds: it reuses the
     * approval type with amount 0 (there is no separate rejection enum value).
     */
    if (!tx_init_common(out, pool_addr, provider_addr, 0.0,
                        TX_CLAIM_APPROVAL, nonce, now))
        return 0;
    tx_set_ref(out, ref_claim_id);
    return tx_compute_id(out);
}

int tx_make_claim_settlement(Transaction *out, const char *pool_addr,
                             const char *provider_addr, double amount,
                             const char *ref_approval_id, uint64_t nonce,
                             time_t now)
{
    if (pool_addr == NULL || provider_addr == NULL) return 0;
    if (!tx_init_common(out, pool_addr, provider_addr, amount,
                        TX_CLAIM_SETTLEMENT, nonce, now))
        return 0;
    tx_set_ref(out, ref_approval_id);
    return tx_compute_id(out);
}

int tx_make_token_transfer(Transaction *out, const char *sender_addr,
                           const char *receiver_addr, double amount,
                           uint64_t nonce, time_t now)
{
    if (sender_addr == NULL || receiver_addr == NULL) return 0;
    if (!tx_init_common(out, sender_addr, receiver_addr, amount,
                        TX_TOKEN_TRANSFER, nonce, now))
        return 0;
    return tx_compute_id(out);
}

int tx_make_coinbase(Transaction *out, const char *miner_addr, double reward,
                     time_t now)
{
    if (miner_addr == NULL) return 0;
    /* Minted coins have no sender; the reward is not user-signed. */
    if (!tx_init_common(out, NULL, miner_addr, reward,
                        TX_COINBASE, 0, now))
        return 0;
    return tx_compute_id(out);
}

const char *tx_type_name(TransactionType type)
{
    switch (type) {
    case TX_POLICY_ENROLLMENT:       return "POLICY_ENROLLMENT";
    case TX_PREMIUM_PAYMENT:         return "PREMIUM_PAYMENT";
    case TX_POLICY_RENEWAL:          return "POLICY_RENEWAL";
    case TX_HEALTHCARE_SERVICE:      return "HEALTHCARE_SERVICE";
    case TX_PRE_AUTHORIZATION:       return "PRE_AUTHORIZATION";
    case TX_CLAIM_SUBMISSION:        return "CLAIM_SUBMISSION";
    case TX_CLAIM_APPROVAL:          return "CLAIM_APPROVAL";
    case TX_CLAIM_SETTLEMENT:        return "CLAIM_SETTLEMENT";
    case TX_TOKEN_TRANSFER:          return "TOKEN_TRANSFER";
    case TX_REINSURANCE_CONTRIBUTION: return "REINSURANCE_CONTRIBUTION";
    case TX_COINBASE:                return "COINBASE";
    default:                         return "UNKNOWN";
    }
}
