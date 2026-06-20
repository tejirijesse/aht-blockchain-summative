#ifndef AHT_INSURANCE_H
#define AHT_INSURANCE_H

#include "types.h"

Policy *insurance_find_policy_by_id(ChainState *chain, const char *policy_id);
Policy *insurance_find_policy_by_member(ChainState *chain, const char *member_address);
const Policy *insurance_find_policy_const_by_id(const ChainState *chain, const char *policy_id);
const Policy *insurance_find_policy_const_by_member(const ChainState *chain, const char *member_address);
const char *insurance_policy_status_name(PolicyStatus status);
int insurance_enroll_policy(ChainState *chain, const char *policy_id,
                            const char *member_address, const char *coverage_plan,
                            time_t now);
int insurance_renew_policy(ChainState *chain, const char *policy_id, time_t now);
int insurance_refresh_policy_status(Policy *policy, time_t now);
int insurance_claim_allowed(ChainState *chain, const char *policy_id, time_t now,
                            char *reason, int reason_len);

#endif
