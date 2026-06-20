# Demo Video Plan

## Goal

Produce a 5 to 10 minute demo that shows the main insurance blockchain workflow clearly and confidently.

## Recommended Structure

### 1. Opening overview
Say:

- this is a blockchain-based health insurance management system in C
- it uses ALU Health Token for insurance activity
- it demonstrates signed transactions, mempool handling, mining, verification, and reinsurance logic

### 2. Build and show commands
Run:

```bash
make clean all
./aht
```

Briefly explain that the CLI exposes wallet, policy, claim, mempool, mining, and verification operations.

### 3. Core happy-path demo
Run:

```bash
mkdir -p demo-data
./aht init demo-data/demo.chain miner-001 1 50
./aht wallet-create demo-data/member.wallet
./aht wallet-create demo-data/provider.wallet
./aht account-add demo-data/demo.chain member MemberOne demo-data/member.wallet 10000
./aht account-add demo-data/demo.chain provider ProviderOne demo-data/provider.wallet 500
./aht policy-enroll demo-data/demo.chain POL001 demo-data/member.wallet standard 5000
./aht premium-pay demo-data/demo.chain demo-data/member.wallet 5000
./aht claim-submit demo-data/demo.chain POL001 demo-data/provider.wallet 1200 svc-001
./aht mine-pending demo-data/demo.chain miner-001
provider_addr=$(sed -n 's/^address=//p' demo-data/provider.wallet)
./aht claim-approve demo-data/demo.chain "$provider_addr" 1200 CLM-00001
./aht mine-pending demo-data/demo.chain miner-001
./aht claim-settle demo-data/demo.chain "$provider_addr" 1200 APR-00001
./aht mine-pending demo-data/demo.chain miner-001
./aht verify demo-data/demo.chain
./aht status demo-data/demo.chain
./aht mempool-list demo-data/demo.chain
```

Talking points:

- policy enrolment records the member on chain
- premium payment automatically creates a reinsurance contribution
- mining batches pending transactions into blocks
- approval and settlement are linked through transaction references
- verification proves the chain is internally consistent

### 4. Reinsurance shortfall example
Use a smaller premium so the reinsurance pool is underfunded.

Run:

```bash
mkdir -p shortfall-data
./aht init shortfall-data/demo.chain miner-001 1 50
./aht wallet-create shortfall-data/member.wallet
./aht wallet-create shortfall-data/provider.wallet
./aht account-add shortfall-data/demo.chain member MemberOne shortfall-data/member.wallet 1500
./aht account-add shortfall-data/demo.chain provider ProviderOne shortfall-data/provider.wallet 500
./aht policy-enroll shortfall-data/demo.chain POL001 shortfall-data/member.wallet standard 200
./aht premium-pay shortfall-data/demo.chain shortfall-data/member.wallet 200
./aht claim-submit shortfall-data/demo.chain POL001 shortfall-data/provider.wallet 1200 svc-001
./aht mine-pending shortfall-data/demo.chain miner-001
provider_addr=$(sed -n 's/^address=//p' shortfall-data/provider.wallet)
./aht claim-approve shortfall-data/demo.chain "$provider_addr" 1200 CLM-00001
./aht mine-pending shortfall-data/demo.chain miner-001
./aht claim-settle shortfall-data/demo.chain "$provider_addr" 1200 APR-00001
```

Talking point:

- because only 10 AHT entered reinsurance, the excess cannot be fully covered and manual review is triggered

### 5. Tamper detection
Run:

```bash
./aht demo
```

Talking point:

- the built-in demo shows verification passing on a clean chain and failing after tampering, proving immutability checks work

## What to emphasize on camera

- deterministic transaction and block hashing
- signature-based authorization
- separation between block nonce and account nonce
- mempool prioritization
- reinsurance logic
- chain verification and tamper detection

## Final line

“This project shows how a low-level blockchain system can be adapted to a real health insurance workflow with auditable policy, premium, claim, and settlement logic.”
