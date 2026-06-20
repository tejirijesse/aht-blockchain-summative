/*
 * transaction.h - Transaction construction, identity, and signing for the
 * nine health-insurance transaction types of the AHT system.
 *
 * A transaction never stores its own hash; instead its identity is derived on
 * demand from its signable fields via tx_compute_id(). The id is the SHA-256 of
 * a canonical serialisation (the same field order used by the Merkle leaf
 * hash, minus the id and signature themselves), so any change to a confirmed
 * transaction's content yields a different id and therefore a different Merkle
 * root.
 *
 * Signing flow:
 *   1. Build the transaction with one of the tx_make_* constructors.
 *   2. tx_compute_id() fills transaction_id.
 *   3. tx_sign() signs the canonical signable bytes with the sender's private
 *      key (ECDSA/secp256k1 via crypto.c) and fills digital_signature.
 *   4. tx_verify_signature() checks that signature against a public key.
 *
 * The nine domain transaction types (plus the auto-generated reinsurance
 * contribution and the coinbase reward) are defined by TransactionType in
 * types.h. Helpers here encode the domain rules: policy enrolment sets a
 * 365-day expiry, premium payment exposes the 5% reinsurance amount, claim
 * settlement exposes the >1000 AHT pool/reinsurance split, etc.
 */
#ifndef AHT_TRANSACTION_H
#define AHT_TRANSACTION_H

#include "types.h"
#include "crypto.h"

/*
 * Serialise the signable fields of `tx` into `out` (capacity `out_cap`) as a
 * canonical, deterministic string. Field order matches merkle_leaf_hash but
 * excludes transaction_id and digital_signature (the fields that are derived
 * from this serialisation). Returns the string length, or 0 on bad input.
 */
size_t tx_serialize_signable(const Transaction *tx, char *out, size_t out_cap);

/*
 * Compute the transaction id (SHA-256 of the signable serialisation) and write
 * it into tx->transaction_id. Returns 1 on success, 0 on bad input.
 */
int tx_compute_id(Transaction *tx);

/*
 * Sign the transaction's signable bytes with `private_key_hex` and store the
 * hex DER signature in tx->digital_signature. tx_compute_id() should have been
 * called first. Returns 1 on success, 0 on failure.
 */
int tx_sign(Transaction *tx, const char *private_key_hex);

/*
 * Verify tx->digital_signature over the signable bytes against
 * `public_key_hex`. Returns 1 if valid, 0 otherwise.
 */
int tx_verify_signature(const Transaction *tx, const char *public_key_hex);

/* ---- Transaction constructors (the nine domain types) ------------------- */

/*
 * Each constructor zero-initialises `out`, fills the common fields (addresses,
 * amount, type, timestamp, nonce) and any type-specific fields, then computes
 * the transaction id. Signing is a separate step (tx_sign). All return 1 on
 * success, 0 on bad input.
 *
 * `now` is the current epoch time supplied by the caller so construction stays
 * deterministic and testable.
 */

/* 1. Policy enrolment: sets policy_expiry = now + 365 days. */
int tx_make_policy_enrollment(Transaction *out, const char *member_addr,
                              const char *pool_addr, double premium,
                              uint64_t nonce, time_t now);

/*
 * 2. Premium payment: member -> insurance pool. The caller should additionally
 * generate a reinsurance contribution for tx_reinsurance_amount(premium) via
 * tx_make_reinsurance_contribution(). Returns 1 on success.
 */
int tx_make_premium_payment(Transaction *out, const char *member_addr,
                            const char *pool_addr, double premium,
                            uint64_t nonce, time_t now);

/* The 5% reinsurance contribution that accompanies a premium payment. */
double tx_reinsurance_amount(double premium);

/* Auto-generated 5% contribution: insurance pool -> reinsurance pool. */
int tx_make_reinsurance_contribution(Transaction *out, const char *pool_addr,
                                     const char *reinsurance_addr,
                                     double premium, uint64_t nonce,
                                     time_t now);

/* 3. Policy renewal: extends coverage to now + 365 days. */
int tx_make_policy_renewal(Transaction *out, const char *member_addr,
                           const char *pool_addr, double premium,
                           uint64_t nonce, time_t now);

/* 4. Healthcare service request: member -> provider. */
int tx_make_healthcare_service(Transaction *out, const char *member_addr,
                               const char *provider_addr, double amount,
                               uint64_t nonce, time_t now);

/* 5. Pre-authorisation: provider -> insurance pool, references a service. */
int tx_make_pre_authorization(Transaction *out, const char *provider_addr,
                              const char *pool_addr, double amount,
                              const char *ref_service_id, uint64_t nonce,
                              time_t now);

/*
 * 6. Claim submission: provider -> insurance pool, references a service tx.
 * `policy_expiry` is the member's current policy expiry so downstream logic can
 * reject claims against an EXPIRED policy.
 */
int tx_make_claim_submission(Transaction *out, const char *provider_addr,
                             const char *pool_addr, const char *member_addr, double amount,
                             const char *ref_service_id, time_t policy_expiry,
                             uint64_t nonce, time_t now);

/* 7a. Claim approval: insurance pool acknowledges a submitted claim. */
int tx_make_claim_approval(Transaction *out, const char *pool_addr,
                           const char *provider_addr, double amount,
                           const char *ref_claim_id, uint64_t nonce,
                           time_t now);

/* 7b. Claim rejection: insurance pool rejects a submitted claim (amount 0). */
int tx_make_claim_rejection(Transaction *out, const char *pool_addr,
                            const char *provider_addr,
                            const char *ref_claim_id, uint64_t nonce,
                            time_t now);

/*
 * 8. Claim settlement: insurance pool -> provider, referencing an approval.
 * For amounts over SETTLEMENT_SPLIT_THRESHOLD (1000 AHT) the caller should also
 * emit a reinsurance contribution for the portion above the threshold; the
 * split helper below exposes that portion.
 */
int tx_make_claim_settlement(Transaction *out, const char *pool_addr,
                             const char *provider_addr, double amount,
                             const char *ref_approval_id, uint64_t nonce,
                             time_t now);

/*
 * The reinsurance portion of a settlement: 0 when amount <= threshold,
 * otherwise (amount - SETTLEMENT_SPLIT_THRESHOLD).
 */
double tx_settlement_reinsurance_portion(double amount);

/* 9. Token transfer: arbitrary sender -> receiver of AHT. */
int tx_make_token_transfer(Transaction *out, const char *sender_addr,
                           const char *receiver_addr, double amount,
                           uint64_t nonce, time_t now);

/* Coinbase reward: minted (no sender) -> miner. Not user-signed. */
int tx_make_coinbase(Transaction *out, const char *miner_addr, double reward,
                     time_t now);

/* Human-readable name for a transaction type (for CLI/audit output). */
const char *tx_type_name(TransactionType type);

#endif /* AHT_TRANSACTION_H */
