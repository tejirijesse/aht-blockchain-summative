#include "insurance.h"

#include <stdio.h>
#include <string.h>

Policy *insurance_find_policy_by_id(ChainState *chain, const char *policy_id)
{
    if (chain == NULL || policy_id == NULL) return NULL;
    for (int i = 0; i < chain->policy_count; i++) {
        if (strcmp(chain->policies[i].policy_id, policy_id) == 0)
            return &chain->policies[i];
    }
    return NULL;
}

Policy *insurance_find_policy_by_member(ChainState *chain, const char *member_address)
{
    if (chain == NULL || member_address == NULL) return NULL;
    for (int i = 0; i < chain->policy_count; i++) {
        if (strcmp(chain->policies[i].member_address, member_address) == 0)
            return &chain->policies[i];
    }
    return NULL;
}

const Policy *insurance_find_policy_const_by_id(const ChainState *chain, const char *policy_id)
{
    if (chain == NULL || policy_id == NULL) return NULL;
    for (int i = 0; i < chain->policy_count; i++) {
        if (strcmp(chain->policies[i].policy_id, policy_id) == 0)
            return &chain->policies[i];
    }
    return NULL;
}

const Policy *insurance_find_policy_const_by_member(const ChainState *chain, const char *member_address)
{
    if (chain == NULL || member_address == NULL) return NULL;
    for (int i = 0; i < chain->policy_count; i++) {
        if (strcmp(chain->policies[i].member_address, member_address) == 0)
            return &chain->policies[i];
    }
    return NULL;
}

const char *insurance_policy_status_name(PolicyStatus status)
{
    switch (status) {
    case POLICY_STATUS_ACTIVE: return "ACTIVE";
    case POLICY_STATUS_EXPIRED: return "EXPIRED";
    case POLICY_STATUS_RENEWED: return "RENEWED";
    default: return "NONE";
    }
}

int insurance_refresh_policy_status(Policy *policy, time_t now)
{
    if (policy == NULL) return 0;
    if (policy->expiry_date < now &&
        (policy->status == POLICY_STATUS_ACTIVE || policy->status == POLICY_STATUS_RENEWED)) {
        policy->status = POLICY_STATUS_EXPIRED;
    }
    return 1;
}

int insurance_enroll_policy(ChainState *chain, const char *policy_id,
                            const char *member_address, const char *coverage_plan,
                            time_t now)
{
    Policy *policy;

    if (chain == NULL || policy_id == NULL || member_address == NULL || coverage_plan == NULL)
        return 0;
    if (chain->policy_count >= MAX_POLICIES) return 0;
    if (insurance_find_policy_by_id(chain, policy_id) != NULL) return 0;

    policy = &chain->policies[chain->policy_count];
    memset(policy, 0, sizeof(*policy));
    snprintf(policy->policy_id, sizeof(policy->policy_id), "%s", policy_id);
    snprintf(policy->member_address, sizeof(policy->member_address), "%s", member_address);
    snprintf(policy->coverage_plan, sizeof(policy->coverage_plan), "%s", coverage_plan);
    policy->enrollment_date = now;
    policy->expiry_date = now + (time_t)POLICY_DURATION_DAYS * SECONDS_PER_DAY;
    policy->status = POLICY_STATUS_ACTIVE;
    chain->policy_count++;
    return 1;
}

int insurance_renew_policy(ChainState *chain, const char *policy_id, time_t now)
{
    Policy *policy = insurance_find_policy_by_id(chain, policy_id);
    if (policy == NULL) return 0;
    policy->expiry_date = now + (time_t)POLICY_DURATION_DAYS * SECONDS_PER_DAY;
    policy->status = POLICY_STATUS_RENEWED;
    return 1;
}

int insurance_claim_allowed(ChainState *chain, const char *policy_id, time_t now,
                            char *reason, int reason_len)
{
    Policy *policy = insurance_find_policy_by_id(chain, policy_id);

    if (reason != NULL && reason_len > 0) reason[0] = '\0';
    if (policy == NULL) {
        if (reason != NULL && reason_len > 0) snprintf(reason, (size_t)reason_len, "policy not found");
        return 0;
    }

    insurance_refresh_policy_status(policy, now);

    if (policy->status == POLICY_STATUS_EXPIRED) {
        if (reason != NULL && reason_len > 0) snprintf(reason, (size_t)reason_len, "policy expired");
        return 0;
    }

    if (reason != NULL && reason_len > 0) snprintf(reason, (size_t)reason_len, "ok");
    return 1;
}
