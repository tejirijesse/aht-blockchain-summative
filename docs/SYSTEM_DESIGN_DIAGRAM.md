# System Design Diagram

This file provides the design diagrams required for submission using Mermaid.
If your final submission format prefers images, you can paste these diagrams into
Mermaid Live Editor or GitHub Markdown rendering and export screenshots.

## 1. Overall System Architecture

```mermaid
flowchart TD
    CLI[CLI Commands] --> INS[Insurance Logic]
    CLI --> MP[Mempool]
    CLI --> MIN[Mining Engine]
    INS --> TX[Transaction Constructors]
    TX --> CR[Crypto and Signatures]
    TX --> MP
    MP --> FR[Fraud Screening]
    FR --> MIN
    MIN --> MK[Merkle Tree]
    MIN --> BL[Block Assembly]
    BL --> BC[Blockchain State]
    BC --> AC[Account Model]
    BC --> UX[UTXO Model]
    BC --> DS[Disk Persistence]
```

## 2. Block Structure and Merkle Tree

```mermaid
flowchart TB
    T1[Transaction 1] --> H1[Leaf Hash 1]
    T2[Transaction 2] --> H2[Leaf Hash 2]
    T3[Transaction 3] --> H3[Leaf Hash 3]
    T4[Transaction 4] --> H4[Leaf Hash 4]
    H1 --> P1[Hash H1+H2]
    H2 --> P1
    H3 --> P2[Hash H3+H4]
    H4 --> P2
    P1 --> MR[Merkle Root]
    P2 --> MR
    MR --> BH[Block Header]
    BH --> BID[block_id]
    BH --> TS[timestamp]
    BH --> TC[transaction_count]
    BH --> PC[previous_hash]
    BH --> MROOT[merkle_root]
    BH --> NONCE[block nonce]
    BH --> MID[miner_id]
    BH --> DIFF[difficulty]
```

## 3. Block Nonce vs Account Nonce

```mermaid
flowchart LR
    BN[Block Nonce] --> B1[Changed repeatedly during PoW]
    BN --> B2[Belongs to block header]
    BN --> B3[Used to satisfy difficulty target]

    AN[Account Nonce] --> A1[Belongs to sender account]
    AN --> A2[Copied into transaction as sender_nonce]
    AN --> A3[Used for replay protection]
    AN --> A4[Increments only after block confirmation]
```

## 4. Mempool Flow with Priority and Fraud Flagging

```mermaid
flowchart TD
    SUB[Submitted Transaction] --> SIG[Signature and input validation]
    SIG --> ADD[Add to mempool as PENDING]
    ADD --> SORT[Order by fee descending then timestamp ascending]
    ADD --> SCAN[Fraud scan]
    SCAN -->|clean| ELIGIBLE[Eligible for mining]
    SCAN -->|suspicious| HOLD[Remain in mempool as SUSPICIOUS]
    ELIGIBLE --> PICK[Miner selects top N]
    PICK --> BLOCK[Included in block]
    BLOCK --> CONF[Status becomes CONFIRMED]
```

## 5. UTXO Transaction Flow

```mermaid
flowchart LR
    U1[Existing UTXO] --> VAL[Validate unspent output]
    VAL --> CONS[Consume spent output]
    CONS --> NEW1[Create provider output]
    CONS --> NEW2[Create change output if any]
    NEW1 --> USET[Updated UTXO set]
    NEW2 --> USET
```

## 6. Account-Balance Flow with Nonce Management

```mermaid
flowchart TD
    ACC[Sender Account] --> CHECK1[Check sufficient balance]
    ACC --> CHECK2[Check current nonce]
    CHECK2 --> TX[Create transaction with sender_nonce = account nonce + 1]
    TX --> SIGN[Sign transaction]
    SIGN --> MEM[Mempool]
    MEM --> MINE[Mined into block]
    MINE --> APPLY[Apply account update]
    APPLY --> BAL[Decrease sender balance / increase receiver balance]
    APPLY --> NON[Increment sender account nonce]
```

## 7. Mining Workflow and Difficulty Retargeting

```mermaid
flowchart TD
    MP[Mempool] --> SEL[Select top pending transactions]
    SEL --> COIN[Prepend coinbase reward transaction]
    COIN --> BUILD[Build candidate block]
    BUILD --> ROOT[Compute Merkle root]
    BUILD --> POW[Iterate block nonce and hash]
    POW -->|valid hash| APPEND[Append block to chain]
    APPEND --> PURGE[Mark confirmed and purge mempool]
    APPEND --> RET[Check retarget window]
    RET -->|after each mine once window exists| ADJ[Adjust difficulty]
```

## 8. Reinsurance Contribution and Disbursement Flow

```mermaid
flowchart TD
    PREM[Premium Payment] --> ROUTE[Auto-calculate 5 percent]
    ROUTE --> INSPOOL[Insurance Pool]
    ROUTE --> REPOOL[Reinsurance Pool Contribution]

    CLAIM[Approved Claim over 1000 AHT] --> SPLIT[Split settlement]
    SPLIT --> PAY1[Insurance Pool pays first 1000]
    SPLIT --> PAY2[Reinsurance Pool pays excess]
    PAY2 --> CHECK[Check available reinsurance balance]
    CHECK -->|enough| FULL[Full excess paid]
    CHECK -->|not enough| PARTIAL[Partial excess paid and manual review flagged]
```

## 9. Fraud Detection Pipeline

```mermaid
flowchart TD
    TXIN[Incoming Transaction] --> R1[Duplicate ID check]
    R1 --> R2[Nonce anomaly check]
    R2 --> R3[Overspend check]
    R3 --> R4[Reference integrity check]
    R4 --> R5[Large transfer and self-transfer checks]
    R5 --> DEC{Suspicious?}
    DEC -->|No| PEND[PENDING]
    DEC -->|Yes| SUSP[SUSPICIOUS]
    SUSP --> HOLD[Held out of automatic mining]
    PEND --> MINING[Normal mining selection]
```
