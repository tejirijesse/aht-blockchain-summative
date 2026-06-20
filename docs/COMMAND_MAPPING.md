# Command Mapping for Submission

The assignment brief uses generic command labels. The current implementation uses a compact CLI with equivalent operations under different names.

| Assignment-style command | Implemented CLI command |
|---|---|
| `register_member` | `account-add <chain-file> member <label> <wallet-file> [balance]` |
| `wallet_balance` | `balance <chain-file> <address>` |
| `enroll_policy` | `policy-enroll <chain-file> <policy-id> <member-wallet-file> <coverage-plan> [premium]` |
| `view_policy` / `policy_status` | `policy-list <chain-file>` |
| `renew_policy` | `policy-renew <chain-file> <policy-id> <member-wallet-file> [premium]` |
| `pay_premium` | `premium-pay <chain-file> <member-wallet-file> <amount>` |
| `submit_claim` | `claim-submit <chain-file> <policy-id> <provider-wallet-file> <amount> <service-ref>` |
| `approve_claim` | `claim-approve <chain-file> <provider-address> <amount> <claim-id-or-txid>` |
| `settle_claim` | `claim-settle <chain-file> <provider-address> <amount> <approval-id-or-txid>` |
| `reinsurance_balance` | `reinsurance-balance <chain-file>` |
| `mempool_view` | `mempool-list <chain-file>` |
| `mine_solo` | `mine-pending <chain-file> <miner-id>` |
| `blockchain_verify` | `verify <chain-file>` |
| `chain_save` | automatic save after state-changing operations |
| `chain_load` | automatic load in commands that operate on an existing chain file |
| `difficulty_status` | `status <chain-file>` |
| `fraud_review` | `fraud-scan <chain-file>` plus `mempool-list <chain-file>` |
| `account_balance` | `balance <chain-file> <address>` |

## Note

The submission should describe the implemented operations by capability, not only by exact command spelling. The core behaviors are present even where the command labels differ from the wording in the brief.
