# Technical Report: Blockchain-Based Health Insurance Management System for ALU

## 1. Introduction

This project implements a blockchain-based health insurance management system in C11 for the African Leadership University (ALU). The system models a decentralized insurance workflow around a native token called the ALU Health Token (AHT). Instead of storing records in a traditional centralized database, the platform represents insurance actions as signed transactions on a proof-of-work blockchain. The goal is to demonstrate transparency, tamper-evidence, traceability, and rule-based automation for policy enrolment, premium collection, claims, and settlement.

The implementation is organized as a modular low-level system. Cryptography, Merkle construction, transaction creation, mempool management, block hashing, blockchain verification, mining, accounts, UTXO tracking, fraud screening, and insurance policy management are all separated into distinct modules. This makes the project easier to reason about, test, and extend.

At a high level, the chain supports the following lifecycle:

- wallet generation for members and providers
- account registration for participants and system wallets
- policy enrolment and renewal
- premium payment in AHT
- automatic 5% reinsurance contribution from premium payments
- claim submission by a provider
- claim approval by the insurance pool
- claim settlement with split payout when the amount exceeds 1,000 AHT
- blockchain verification and tamper detection

The system also persists its full state to disk and can restore it later after verifying the stored chain before permitting further work.

## 2. Blockchain Architecture and Data Structures

The main runtime object is `ChainState`. It stores all persistent blockchain state in one structure:

- block array and block count
- mempool
- UTXO set
- account registry and account nonces
- token metadata
- difficulty and block reward
- insurance pool and reinsurance pool balances
- policy registry

### 2.1 Block structure

Each block contains:

- `block_id`
- `timestamp`
- `transaction_count`
- `previous_hash`
- `merkle_root`
- `nonce`
- `miner_id`
- `difficulty`
- `transactions[]`
- `block_hash`

The block links to the previous block using `previous_hash`. Any mutation of a historical block breaks this linkage and is caught during verification. `merkle_root` summarizes the transaction batch, while `nonce` is the proof-of-work search value changed during mining. `difficulty` records the target active when the block was mined.

### 2.2 Transaction structure

Each transaction stores:

- `transaction_id`
- `sender_address`
- `receiver_address`
- `amount`
- `transaction_type`
- `timestamp`
- `sender_nonce`
- `digital_signature`
- domain metadata such as `policy_expiry` and `ref_transaction_id`

The implementation uses a deterministic serialized form of the signable fields to derive the transaction identifier. The important design point is that the transaction does not store the temporary hash values used during signing or during Merkle tree construction. Instead:

- a temporary digest is produced to sign a transaction
- a temporary digest is produced again when the miner builds Merkle leaves
- those temporary digests are discarded
- the transaction keeps only its logical fields and a stable `transaction_id` used for indexing and references

This distinction matters because the assignment specifically separates transaction fields from temporary hashing operations.

### 2.3 Mempool structure

The mempool is implemented as a fixed-capacity array of `MempoolEntry` records. Each entry contains:

- the transaction
- a fee
- a status

The statuses used in the implementation are:

- `PENDING`
- `CONFIRMED`
- `SUSPICIOUS`
- `REJECTED`

Selection for mining follows fee-descending and timestamp-ascending ordering, which matches the prioritization rules in the brief.

### 2.4 Token structure

The AHT token stores:

- token name
- token symbol
- total supply

Total supply increases through coinbase reward minting during mining.

## 3. Why Transactions Do Not Store Their Own Hash

A common blockchain misconception is to treat a transaction as if it permanently stores every hash associated with it. This project follows a cleaner design.

A transaction participates in two main hashing contexts:

1. Signing: the signable transaction fields are serialized, hashed, and then signed using ECDSA.
2. Merkle construction: the transaction fields are serialized again and hashed to form a leaf node.

These are temporary operations. The exact digest produced in each context is not retained as a standalone field because:

- signatures can be verified by recomputing the digest from the canonical fields
- Merkle leaves can be recomputed from the canonical fields during verification
- persisting temporary digests would duplicate derived state and increase the risk of inconsistency

Instead, the project uses a deterministic `transaction_id` for identification and reference linking. In other words, the transaction keeps an identifier, but not the disposable intermediate digests used inside cryptographic workflows.

## 4. The Difference Between Block Nonce and Account Nonce

One of the most important conceptual distinctions in the project is the difference between the block nonce and the account nonce.

### 4.1 Block nonce

The block nonce is a mining field. It belongs to the block header, not to any account. During proof-of-work, the miner repeatedly modifies the block nonce and recomputes the block hash until the hash satisfies the difficulty target. This process provides computational effort and chain sealing.

The block nonce is therefore:

- block-scoped
- changed during mining only
- unrelated to replay protection

### 4.2 Account nonce

The account nonce is a replay-protection field. Each account starts at nonce 0. When an account creates a transaction, it copies its expected next nonce into the transaction’s `sender_nonce` field. The network validates that the incoming nonce equals the account’s current nonce plus one. If it does not, the transaction is rejected.

The account nonce is therefore:

- account-scoped
- part of transaction validation
- incremented only when a transaction is confirmed in a mined block
- included in the signed payload so it cannot be altered later

This separation prevents confusion between mining state and account ordering state.

## 5. Merkle Tree Construction and Verification

The Merkle tree implementation is built from scratch. The miner computes a leaf hash for each transaction using the transaction’s canonical serialization. These leaf hashes are paired and rehashed level by level until a single root remains.

Rules followed by the implementation:

- each leaf is based on deterministic transaction bytes
- hashes are combined pairwise
- if a level contains an odd number of nodes, the last hash is duplicated before hashing upward
- the final root is stored in the block header as `merkle_root`

During verification, the root is recomputed from the block’s transactions and compared against the stored root. If any transaction was changed after mining, the recomputed Merkle root differs and the block fails verification.

## 6. UTXO Model Implementation

The project includes a UTXO subsystem alongside the account-balance model. This is important because the assignment requires both transaction models.

The UTXO set stores:

- originating transaction id
- output index
- owner address
- amount
- spent flag

The implementation supports:

- UTXO creation when value-moving transactions produce outputs
- validation of available unspent outputs
- consumption of spent outputs
- rejection of double-spending

This model mirrors Bitcoin-style value flow. Instead of merely overwriting balances, spending consumes old outputs and creates new ones. In this project, the UTXO layer acts as a parallel ledger used to demonstrate output-based accounting and double-spend prevention.

## 7. Account-Based Model and Replay Protection

The account model follows an Ethereum-like structure. Each account stores:

- role
- address
- public key
- balance
- nonce
- label

The system validates:

- sender existence
- sufficient balance for value-moving transactions
- nonce correctness
- signature correctness

When a block is confirmed, account balances are updated and the sender nonce increments. Because the transaction includes `sender_nonce` inside the signed serialization, replaying or modifying a transaction breaks verification.

The required roles are represented in the implementation:

- member
- provider
- insurance pool
- miner
- reinsurance pool

The insurance pool and reinsurance pool also have deterministic system wallets for repeatable demonstrations and internal signing flows.

## 8. Reinsurance Pool Design and Settlement Logic

The reinsurance pool is a second-tier risk buffer. Its design reflects two linked rules from the brief.

First, each premium payment automatically triggers a second transaction: a `REINSURANCE_CONTRIBUTION` equal to 5% of the premium. This amount is transferred from the insurance pool to the reinsurance pool.

Second, if a claim settlement exceeds 1,000 AHT, the system splits the settlement:

- the insurance pool pays the first 1,000 AHT
- the reinsurance pool pays the remainder

If the reinsurance pool cannot cover the entire remainder, the project does not allow the pool to go negative. Instead, it pays only what is available and flags the shortfall for manual review. This behavior preserves accounting correctness and aligns with the safety rule in the brief.

## 9. Mining and Difficulty Retargeting

Mining is implemented with proof-of-work. The miner:

- selects top-priority pending transactions from the mempool
- computes the Merkle root
- constructs the next block
- prepends a coinbase reward transaction
- searches block nonces until the block hash has enough leading zero hex digits
- appends the block to the chain
- confirms the included transactions
- applies account and UTXO effects
- purges finalized mempool entries

Difficulty retargeting is implemented over a ten-block window. The system compares the recent average block time against the configured thresholds:

- if average time is below the lower target, difficulty increases
- if average time is above the upper target, difficulty decreases but not below 1
- otherwise difficulty remains unchanged

The chain stores the current difficulty and applies retargeting after mined blocks.

## 10. ECDSA Key Generation, Signing, and Verification

The project uses secp256k1-based ECDSA through OpenSSL-backed cryptographic helpers.

The flow is:

1. generate a keypair
2. derive a printable wallet address from the public key
3. serialize the transaction canonically
4. hash the serialized bytes
5. sign the digest with the private key
6. store the signature hex in `digital_signature`
7. verify the signature before acceptance or block inclusion

This proves authorization and prevents forged state transitions. Members and providers can export wallet files containing address, public key, and private key for CLI-driven transaction creation.

## 11. Fraud Detection Heuristics and Review Workflow

The project contains a fraud screening layer that analyzes transactions before mining. Suspicious transactions remain in the mempool and are not automatically selected into blocks.

The implemented fraud logic includes checks such as:

- duplicate transaction identifiers
- nonce anomalies
- overspending
- self-transfers
- large transfers
- dangling references for claim approval and settlement links

When a transaction is flagged, its mempool status becomes `SUSPICIOUS`. This preserves the audit trail while withholding it from normal mining selection.

The current CLI directly exposes `fraud-scan` and `mempool-list`. The review model is therefore partially implemented operationally through scanning and status transitions, even though the assignment’s named commands `approve_suspicious` and `reject_suspicious` are not exposed under those exact labels in the current CLI.

## 12. Disk Persistence Format and Record Layout

The system uses binary persistence. The on-disk file begins with a small header:

- magic bytes: `AHTCHAIN`
- version number: `2`

After the header, the program writes the raw `ChainState` structure. This means the saved file contains:

- all blocks and their transactions
- mempool state
- UTXO set
- accounts and nonces
- token metadata
- difficulty, reward, and pool balances
- policy registry

On load, the program:

- validates the file header
- validates bounds on block count, mempool size, UTXO count, and account count
- restores the structure into memory
- verifies the blockchain before continuing

This satisfies the persistence and verify-on-load requirement.

## 13. Policy Lifecycle Management

The policy registry stores:

- policy id
- member address
- coverage plan
- enrollment date
- expiry date
- status

The lifecycle rules are:

- new policy: status becomes `ACTIVE`
- renewal: expiry resets to now plus 365 days and status becomes `RENEWED`
- expired policy: status transitions to `EXPIRED` once the current time passes expiry
- claims against expired or missing policies are rejected

This creates a realistic insurance flow instead of a generic payment chain.

## 14. Documented Test Scenarios

### Scenario 1: Core chain verification and tamper detection

Expected result:

- a clean chain verifies successfully
- a reloaded chain also verifies successfully
- tampering with `previous_hash` causes verification failure

Actual output:

```text
init: 1 (block_count=1)
genesis hash: a63c35f00a009550afd27f9b6a9ba4353e0babfb7c1cdd2eca11202ea7c85a39
make_next: 1 (block_id=1)
mine: 1 (nonce=30, hash=00c2006aec4bcf63795df945618b0c54f9376bd1d68a9f112dabfbae685c369a)
append: 1 (block_count=2)
append2: 1 (block_count=3)
verify (clean): 1
save: 1
load: 1 (block_count=3)
verify (reloaded): 1
verify (tampered prev_hash): 0
```

Interpretation:

The chain passes clean verification, survives persistence and reload, and then correctly fails once a historical linkage field is tampered with.

### Scenario 2: Premium payment with automatic reinsurance contribution

Expected result:

- premium payment enters the mempool
- a reinsurance contribution transaction is generated automatically
- mining confirms both together
- the reinsurance pool balance increases by 5% of the premium

Actual output excerpt:

```text
premium payment queued: 70a04480f3aaecb1271c9afbc3a14a16355e2a4454f2bb79df81e102c456ff4e
reinsurance contribution queued: fce819958012d73f512e275519cfd5cfe6f2ad59f147af864abd0f72ee3fe3a0
mined block at height 1 with 4 selected txs
reinsurance_pool_balance: 10.00
```

Interpretation:

A 200 AHT premium generated a 10 AHT reinsurance contribution, which matches the 5% rule exactly.

### Scenario 3: High-value claim with underfunded reinsurance pool

Expected result:

- the insurance pool pays the first 1,000 AHT
- the reinsurance pool attempts to pay the remaining 200 AHT
- if the reinsurance pool only has 10 AHT, the system does not overdraw it
- the shortfall is flagged for manual review

Actual output:

```text
claim settlement queued: 98d35f7a0c9e489dfed2d984c21c53ad9c9a06064f7d98d12a8cce69f341d390
reinsurance settlement queued as suspicious: 769370bf49b2f4651c7ef18a015d5fe8396d626f3e3cf4815652427a12eb9be5 (clean)
manual review required: reinsurance pool could only cover 10.00 of 200.00 excess
```

Interpretation:

The split-settlement rule worked correctly, and the implementation preserved the non-negative balance guarantee for the reinsurance pool.

## 15. Conclusion

The ALU Health Token blockchain demonstrates the core mechanics of blockchain engineering in a domain-specific insurance setting. It combines proof-of-work, Merkle commitments, ECDSA authorization, replay protection, UTXO validation, account-based balances, fraud screening, binary persistence, and policy lifecycle logic in a single coherent C11 system.

The strongest aspects of the implementation are its deterministic cryptographic flow, tamper-detection behavior, modular structure, and realistic insurance settlement design. The project also shows how blockchain concepts can be applied beyond currency transfer into operational workflows such as enrolment, claims handling, and auditability.

From a submission perspective, the system is best understood as a technically solid educational blockchain with a health-insurance transaction model layered on top. The codebase, README, and supporting documents together provide a complete picture of the architecture, rules, behavior, and tested outcomes.
