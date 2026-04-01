#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dasall::infra::policy {

enum class PolicyDecision {
  Unspecified = 0,
  Allow = 1,
  Deny = 2,
  RequireConfirmation = 3,
};

struct PolicyQueryContext {
  std::string module;
  std::string operation;
  std::string target_type;
  std::string target_ref;
  std::string actor_ref;
  std::string request_id = "unknown";
  std::string session_id = "unknown";
  std::string trace_id = "unknown";
  std::string task_id = "unknown";
  std::string profile_id = "unknown";

  [[nodiscard]] bool has_required_fields() const {
    return !module.empty() && !operation.empty() && !target_type.empty() &&
           !target_ref.empty() && !actor_ref.empty();
  }
};

struct PolicyDecisionRef {
  PolicyDecision decision = PolicyDecision::Unspecified;
  std::string reason_code;
  std::vector<std::string> matched_rule_ids;
  std::string snapshot_id;
  std::uint64_t generation = 0;
  std::string evidence_ref;
  std::vector<std::string> warnings;

  [[nodiscard]] bool is_valid() const {
    return decision != PolicyDecision::Unspecified && !reason_code.empty() &&
           !matched_rule_ids.empty() && !snapshot_id.empty() && generation > 0 &&
           !evidence_ref.empty();
  }
};

}  // namespace dasall::infra::policy