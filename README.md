# AHT Blockchain — ALU Health Token

A from-scratch blockchain implemented in C11 that powers a **Health Insurance Management System**. The native asset is the **ALU Health Token (AHT)**. The chain models the full lifecycle of health-insurance activity — policy enrolment, premium payments, healthcare service records, claim submission/approval/settlement, reinsurance contributions, and token transfers — as cryptographically signed, Merkle-committed, proof-of-work-sealed transactions.

---

## Table of Contents

1. [Overview](#overview)
2. [Feature Summary](#feature-summary)
3. [Repository Layout](#repository-layout)
4. [Architecture](#architecture)
5. [Build & Run](#build--run)
6. [Module Reference](#module-reference)
7. [Data Model](#data-model)
8. [Transaction Types & Domain Rules](#transaction-types--domain-rules)
9. [Mining & Consensus](#mining--consensus)
10. [Chain Verification & Tamper Resistance](#chain-verification--tamper-resistance)
11. [Implementation Status](#implementation-status)
12. [Glossary](#glossary)

---

## Overview

The AHT blockchain is an academic, self-contained ledger written in portable C11. It demonstrates the core machinery of a real blockchain — cryptographic hashing, digital signatures, Merkle commitments, block linking, proof-of-work, and full-chain verification — while specializing the transaction layer for health-insurance use cases.

Design goals:

- **Determinism** — every derived field (transaction id, Merkle root, block hash) is computed from a canonical, fixed-order serialization so independent nodes agree byte-for-byte.
- **Tamper-evidence** — `merkle_root` and `block_hash` are *always recomputed* on verification and never trusted as stored. Any mutation of history is detectable.
- **Separation of concerns** — cryptography, transactions, mempool, block structure, chain rules, and mining each live in their own module behind a small header API.
- **Domain fidelity** — the transaction model encodes real insurance rules (policy expiry, reinsurance carve-outs, claim settlement splits) rather than generic value transfers.

---

## Feature Summary

| Capability | Status |
|---|---|
| SHA-256 hashing (OpenSSL 3 wrapper) | Implemented |
| ECDSA keygen / sign / verify / address derivation (secp256k1) | Implemented |
| Merkle tree (from scratch, odd-count duplication) | Implemented |
| 9 health-insurance transaction types (+ coinbase, reinsurance) | Implemented |
| Mempool with fee/timestamp prioritization & de-duplication | Implemented |
| Block structure + canonical header serialization | Implemented |
| Blockchain linking, genesis, full verification | Implemented |
| Proof-of-Work mining, difficulty retargeting, solo/pool, coinbase reward | Implemented |
| Wallet generation (secp256k1 keypair + derived address export) | Implemented |
| Basic account registry CLI (add/list persisted accounts) | Implemented |
| Account-Balance model (nonce replay protection) | Implemented |
| UTXO model | Implemented |
| Fraud-detection heuristics | Implemented |
| Policy registry, expiry checks, and renewals | Implemented |
| Premium auto-routing to reinsurance pool | Implemented |
| Claim settlement splitting + partial reinsurance review | Implemented |
| Disk persistence (binary save/load + verify-on-load) | Implemented |
| Interactive CLI for chain operations | Implemented |

---

## Repository Layout

```
aht-blockchain-summative/
├── include/                 # Public module headers
│   ├── types.h              # Central shared structs, enums, constants
│   ├── sha256.h             # SHA-256 wrapper API
│   ├── crypto.h             # ECDSA keygen/sign/verify/address
│   ├── merkle.h             # Merkle tree construction
│   ├── transaction.h        # Transaction constructors, signing, ids
│   ├── mempool.h            # Pending-transaction pool
│   ├── block.h              # Block header serialization & hashing
│   ├── blockchain.h         # Chain assembly, linking, verification
│   ├── mining.h             # Proof-of-Work, retargeting, rewards
│   ├── account.h            # Account-based ledger and nonce checks
│   ├── utxo.h               # UTXO ledger helpers
│   ├── fraud.h              # Fraud screening heuristics
│   └── insurance.h          # Policy lifecycle registry and checks
├── src/                     # Implementations (one .c per header)
│   ├── sha256.c
│   ├── crypto.c
│   ├── merkle.c
│   ├── transaction.c
│   ├── mempool.c
│   ├── block.c
│   ├── blockchain.c
│   ├── mining.c
│   ├── account.c
│   ├── utxo.c
│   ├── fraud.c
│   ├── insurance.c
│   └── chain_test.c         # CLI entry point and demo driver
├── build/                   # Build artifacts (gitignored)
├── data/                    # Persisted chain files (gitignored)
├── docs/                    # Technical report & design diagrams
├── Makefile
├── .gitignore
└── README.md
```

---

## Architecture

The system is layered. Each layer depends only on the ones beneath it:

```
            ┌──────────────────────────────────────────┐
            │                  CLI                      │
            └──────────────────────────────────────────┘
            ┌──────────────────────────────────────────┐
            │   Mining (PoW · retarget · reward)        │
            └──────────────────────────────────────────┘
            ┌──────────────────────────────────────────┐
            │   Blockchain (link · genesis · verify)    │
            └──────────────────────────────────────────┘
            ┌────────────────────┬─────────────────────┐
            │   Block            │   Mempool           │
            └────────────────────┴─────────────────────┘
            ┌──────────────────────────────────────────┐
            │   Transaction (9 health-insurance types)  │
            └──────────────────────────────────────────┘
            ┌──────────┬───────────────┬───────────────┐
            │  Merkle  │  Crypto/ECDSA │   SHA-256     │
            └──────────┴───────────────┴───────────────┘
```

All structs are defined centrally in `types.h`, so modules share a single source of truth for sizes and field layouts.

### Determinism principle

Every hash in the system is derived from a **canonical serialization** with a fixed field order and explicit `|` separators. Transaction ids, Merkle roots, and block hashes are reproducible by any party. The `block_hash` field is deliberately *excluded* from the header serialization it commits to (a hash cannot commit to itself).

---

## Build & Run

### Prerequisites

- A C11 compiler (`gcc` or `clang`).
- **OpenSSL 3** (Homebrew on macOS):

```sh
brew install openssl@3
```

The build expects OpenSSL at `/opt/homebrew/opt/openssl@3` (Apple-Silicon Homebrew default). Adjust the paths in the `Makefile` if your install differs.

### Build with Make

```sh
make
```

This compiles every `src/*.c` into the `aht` binary. The compiler flags are:

```
-Wall -Wextra -std=c11 -g -Iinclude -I/opt/homebrew/opt/openssl@3/include
```

and it links against:

```
-L/opt/homebrew/opt/openssl@3/lib -lssl -lcrypto
```

### Run the CLI

`src/chain_test.c` is the current entry point. Build the project, then use the `aht` binary for chain snapshots, wallet generation, account registration, policy lifecycle operations, claims, fraud review, verification, and mining:

```sh
make
./aht
```

Available commands:

```text
./aht demo
./aht init <chain-file> <miner-id> [difficulty] [reward]
./aht verify <chain-file>
./aht status <chain-file>
./aht wallet-create <wallet-file>
./aht account-add <chain-file> <role> <label> <address-or-wallet-file> [balance]
./aht account-list <chain-file>
./aht balance <chain-file> <address>
./aht policy-enroll <chain-file> <policy-id> <member-wallet-file> <coverage-plan> [premium]
./aht policy-renew <chain-file> <policy-id> <member-wallet-file> [premium]
./aht policy-list <chain-file>
./aht premium-pay <chain-file> <member-wallet-file> <amount>
./aht claim-submit <chain-file> <policy-id> <provider-wallet-file> <amount> <service-ref>
./aht claim-approve <chain-file> <provider-address> <amount> <claim-id-or-txid>
./aht claim-settle <chain-file> <provider-address> <amount> <approval-id-or-txid>
./aht mempool-list <chain-file>
./aht fraud-scan <chain-file>
./aht mine-pending <chain-file> <miner-id>
./aht reinsurance-balance <chain-file>
./aht mine-empty <chain-file> <miner-id>
```

`claim-approve` accepts either a real claim-submission transaction hash or the
sequence form `CLM-00001`. `claim-settle` accepts either the approval
transaction hash or the sequence form `APR-00001`.

Example session:

```sh
mkdir -p data
./aht wallet-create data/miner.wallet
./aht init data/dev.chain miner-001 1 50
./aht wallet-create data/member.wallet
./aht wallet-create data/provider.wallet
./aht account-add data/dev.chain member "MemberOne" data/member.wallet 1500
./aht account-add data/dev.chain provider "ProviderOne" data/provider.wallet 500
./aht policy-enroll data/dev.chain POL001 data/member.wallet standard 200
./aht premium-pay data/dev.chain data/member.wallet 200
./aht claim-submit data/dev.chain POL001 data/provider.wallet 1200 svc-001
./aht mine-pending data/dev.chain miner-001
./aht status data/dev.chain
```

Role values accepted by `account-add` are:

- `member`
- `provider`
- `insurance-pool`
- `miner`
- `reinsurance-pool`

### Demo mode

`./aht demo` still exercises genesis creation, block assembly, mining, appending, snapshot save/load, clean verification, and tamper detection in one run.

> **Note on memory:** `ChainState` is large (the block array dominates). The test driver declares it `static` to keep it off the stack — stack-allocating it would overflow and crash. Any new entry point should do the same.

---

## Module Reference

### `sha256` — Cryptographic hash
A thin wrapper over OpenSSL's SHA-256 that produces 32-byte digests and their 64-char hex encodings. Every other hashing primitive (Merkle nodes, block headers, transaction ids) is built on this.

### `crypto` — ECDSA over secp256k1
Key generation, message signing, signature verification, and deriving a printable wallet **address** from a public key. Transactions are signed over their canonical signable serialization; signatures are stored hex-encoded.

### `merkle` — Merkle tree
Builds a Merkle root from a list of transaction leaves, hashing pairwise up the tree. When a level has an **odd** number of nodes, the last node is **duplicated** to form a pair. The root commits to the entire transaction set; changing any transaction changes the root.

### `transaction` — Health-insurance transactions
Defines the transaction record and constructors for each domain operation (see [Transaction Types](#transaction-types--domain-rules)). Provides:
- `tx_serialize_signable` / `tx_compute_id` — canonical bytes and the transaction id.
- `tx_sign` / `tx_verify_signature` — ECDSA binding.
- `tx_make_coinbase` — the miner-reward transaction.
- Domain helpers: `tx_reinsurance_amount`, `tx_settlement_reinsurance_portion`, `tx_type_name`.

### `mempool` — Pending pool
Holds unconfirmed transactions. Orders candidates **fee-descending, then timestamp-ascending**, de-duplicates by `transaction_id`, and selects a block-sized batch for mining.

### `block` — Block structure & hashing
- `block_recompute_merkle_root` — derive the root from the block's transactions.
- `block_serialize_header` — canonical header bytes (`block_id | timestamp | tx_count | previous_hash | merkle_root | nonce | miner_id | difficulty`), **excluding** `block_hash`.
- `block_compute_hash` — SHA-256 of the serialized header.
- `block_hash_meets_difficulty` — does the hash carry *N* leading `0` hex digits?
- `block_verify_self` — recompute Merkle root and block hash and confirm they match the stored values.

### `blockchain` — Chain rules
- `blockchain_init` — zero the state, set up the AHT token, and mine the **genesis** block (height 0, all-zero `previous_hash`).
- `blockchain_make_next_block` — assemble (but not mine) the next block linked to the tip.
- `blockchain_append_block` — append a mined block after four checks: self-consistency, correct height, links to tip, meets difficulty.
- `blockchain_verify` — walk the whole chain: every block self-consistent, heights sequential, each `previous_hash` matches the prior `block_hash`, genesis prev is all-zero, and each block meets its recorded difficulty.

### `mining` — Proof-of-Work & rewards
- `mining_proof_of_work` — brute-force the nonce until the recomputed `block_hash` meets the difficulty target.
- `mining_mine_block` — one-shot assemble → prepend coinbase → mine → append, growing `total_supply` by the minted reward.
- `mining_retarget_difficulty` — adjust difficulty based on the wall-clock span of the most recent window of blocks.

---

## Data Model

Core constants and structures (defined in `types.h`):

### Sizing constants

| Constant | Value | Meaning |
|---|---|---|
| `HASH_LEN` | 32 | Raw SHA-256 bytes |
| `HASH_HEX_LEN` | 65 | Hex digest + null |
| `ADDRESS_LEN` | 41 | Wallet address string |
| `SIG_HEX_LEN` | 256 | Hex signature buffer |
| `PUBKEY_HEX_LEN` | 142 | Hex public key |
| `MAX_TX_PER_BLOCK` | 128 | Transactions per block |
| `MAX_BLOCKS` | 8192 | Chain capacity |
| `MAX_MEMPOOL` | 2048 | Pending pool capacity |
| `MAX_UTXOS` | 8192 | UTXO set capacity |
| `MAX_ACCOUNTS` | 16 | Account table capacity |
| `POLICY_DURATION_DAYS` | 365 | Policy lifetime |
| `SECONDS_PER_DAY` | 86400 | Time unit |
| `REINSURANCE_RATE` | 0.05 | 5% reinsurance carve-out |
| `SETTLEMENT_SPLIT_THRESHOLD` | 1000.0 | AHT above which settlement splits to reinsurance |

### Key structures

- **`Transaction`** — `transaction_id`, `sender_address`, `receiver_address`, `amount`, `transaction_type`, `timestamp`, `sender_nonce`, `digital_signature`, `policy_expiry`, `ref_transaction_id`.
- **`Block`** — `block_id`, `timestamp`, `transaction_count`, `previous_hash`, `merkle_root`, `nonce`, `miner_id`, `difficulty`, `transactions[MAX_TX_PER_BLOCK]`, `block_hash`.
- **`Token`** — `token_name` ("ALU Health Token"), `token_symbol` ("AHT"), `total_supply`.
- **`ChainState`** — `blocks[MAX_BLOCKS]`, `block_count`, `mempool`, `utxos`, `accounts[MAX_ACCOUNTS]`, `account_count`, `token`, `difficulty`, `block_reward`, `insurance_pool_balance`, `reinsurance_pool_balance`.

### Account roles

`ACC_MEMBER`, `ACC_PROVIDER`, `ACC_INSURANCE_POOL`, `ACC_MINER`, `ACC_REINSURANCE_POOL`.

---

## Transaction Types & Domain Rules

The `TransactionType` enum encodes the insurance domain:

| Type | Purpose |
|---|---|
| `TX_POLICY_ENROLLMENT` | A member enrols in a policy. Sets `policy_expiry = now + 365 days`. |
| `TX_PREMIUM_PAYMENT` | A member pays a premium. A 5% reinsurance amount is derived. |
| `TX_POLICY_RENEWAL` | Extends an existing policy. |
| `TX_HEALTHCARE_SERVICE` | Records a service rendered by a provider. |
| `TX_PRE_AUTHORIZATION` | Pre-approves a planned service. |
| `TX_CLAIM_SUBMISSION` | A member/provider submits a claim. |
| `TX_CLAIM_APPROVAL` | An insurer approves a claim (references the submission). |
| `TX_CLAIM_SETTLEMENT` | Pays out a claim. Amount above 1000 AHT is split to the reinsurance pool. |
| `TX_TOKEN_TRANSFER` | Generic AHT transfer between addresses. |
| `TX_REINSURANCE_CONTRIBUTION` | Moves the reinsurance carve-out into the reinsurance pool. |
| `TX_COINBASE` | Mints the block reward to the miner/pool. |

Claim rejection is represented as an approval-family record with amount `0` referencing the original submission.

### Economic rules

- **Policy expiry:** enrolment stamps `policy_expiry` exactly 365 days ahead (`POLICY_DURATION_DAYS × SECONDS_PER_DAY`).
- **Reinsurance carve-out:** premium payments imply a 5% reinsurance contribution (`REINSURANCE_RATE`).
- **Settlement split:** for a claim settlement, the portion of the amount **above** `SETTLEMENT_SPLIT_THRESHOLD` (1000 AHT) is routed to the reinsurance pool; the remainder is paid normally.

---

## Mining & Consensus

### Proof-of-Work

A block is sealed by finding a `nonce` such that its recomputed `block_hash` has at least `difficulty` leading zero **hex digits**. `mining_proof_of_work` scans nonces `0..max_attempts-1` (a `0` argument selects `MINING_DEFAULT_MAX_ATTEMPTS = 100,000,000`). The Merkle root is *not* recomputed inside the loop — it commits to the transactions, which don't change as the nonce varies — so only the header bytes and resulting hash change per attempt.

### One-shot mine

`mining_mine_block` performs the full path:

1. Build a coinbase paying `chain->block_reward` to the miner (or pool payout) address.
2. Prepend it to the selected transactions (`body = [coinbase, txs...]`).
3. Assemble and link the block to the tip; derive the Merkle root.
4. Run proof-of-work at the chain's current difficulty.
5. Append the sealed block (re-verifying linkage, PoW, and self-consistency).
6. Increase `token.total_supply` by the minted reward.

Solo vs. pool (`MINE_SOLO` / `MINE_POOL`) is purely a labeling distinction for who the coinbase credits — the PoW is identical.

### Difficulty retargeting

`mining_retarget_difficulty` keeps the average block time near `MINING_TARGET_BLOCK_TIME` (30 s). After at least one full window of `MINING_RETARGET_INTERVAL` (10) blocks, it compares the window's actual wall-clock span against the expected span:

- **Too fast** (or non-positive span from clock skew) → difficulty **+1**.
- **Too slow** → difficulty **−1**, never below `MINING_MIN_DIFFICULTY` (1).

### Mining constants

| Constant | Value |
|---|---|
| `MINING_RETARGET_INTERVAL` | 10 blocks |
| `MINING_TARGET_BLOCK_TIME` | 30 seconds |
| `MINING_DEFAULT_MAX_ATTEMPTS` | 100,000,000 |
| `MINING_MIN_DIFFICULTY` | 1 |

---

## Chain Verification & Tamper Resistance

`blockchain_verify` walks the entire chain and rejects any inconsistency:

1. **Self-consistency** — every block's `merkle_root` and `block_hash` are recomputed and must match the stored values (`block_verify_self`).
2. **Sequential heights** — `block_id == index` for all blocks.
3. **Genesis anchor** — block 0 carries the all-zero `previous_hash`.
4. **Linkage** — each block's `previous_hash` equals the prior block's `block_hash`.
5. **Difficulty** — every mined block meets its recorded difficulty target.

Because the two derived fields are never trusted as stored, editing a confirmed transaction, a `previous_hash`, or a count breaks the recomputation and is detected immediately. The test driver demonstrates this: tampering with a confirmed block's `previous_hash` flips verification from `1` to `0`.

---

## Implementation Status

**Done**

- Project scaffold, central `types.h`, Makefile
- SHA-256 wrapper (OpenSSL 3)
- ECDSA crypto (keygen, sign, verify, address)
- Merkle tree (odd-count duplication)
- Transaction layer (9 health-insurance types + coinbase/reinsurance)
- Mempool (fee/timestamp prioritization)
- Block + blockchain (linking, genesis, verification)
- Mining (PoW, difficulty retarget, solo/pool, coinbase reward)
- Account ledger with seeded wallets and nonce replay protection
- UTXO ledger with spend tracking and change outputs
- Fraud screening for mempool admission and review
- Policy registry with enrollment, renewal, and expiry checks
- Disk persistence (binary snapshot save/load + verify-on-load)
- CLI for chain lifecycle, wallet export, signed policy/claim flows, fraud, and mining

**Planned**

- Technical Report & System Design Diagram

---

## Glossary

- **AHT** — ALU Health Token, the chain's native asset.
- **Coinbase** — the special transaction that mints the block reward to the miner.
- **Difficulty** — number of leading zero hex digits a block hash must have.
- **Genesis block** — the first block (height 0) with an all-zero `previous_hash`.
- **Merkle root** — a single hash committing to all transactions in a block.
- **Nonce** — the value miners vary to find a qualifying block hash.
- **Reinsurance pool** — a secondary fund that absorbs carve-outs and large-claim overflow.
- **UTXO** — Unspent Transaction Output (planned alternative accounting model).
