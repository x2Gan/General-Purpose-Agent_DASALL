#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <type_traits>

#include "policy/PolicyTypes.h"
#include "support/TestAssertions.h"

namespace {

void test_policy_query_context_keeps_unknown_defaults_without_empty_drift() {
  using dasall::infra::policy::PolicyQueryContext;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(PolicyQueryContext{}.request_id), std::string>);
  static_assert(std::is_same_v<decltype(PolicyQueryContext{}.profile_id), std::string>);

  const PolicyQueryContext minimal{
      .module = std::string("plugin"),
      .operation = std::string("load"),
      .target_type = std::string("manifest"),
      .target_ref = std::string("plugin.echo"),
      .actor_ref = std::string("system"),
  };

  assert_true(minimal.request_id == "unknown" && minimal.session_id == "unknown" &&
                  minimal.trace_id == "unknown" && minimal.task_id == "unknown" &&
                  minimal.profile_id == "unknown",
              "policy query context should default cross-cutting identifiers to unknown");
  assert_true(minimal.has_required_fields(),
              "policy query context should remain valid when minimal routing fields are present");

  const PolicyQueryContext unknown_fallback{
      .module = std::string("unknown"),
      .operation = std::string("unknown"),
      .target_type = std::string("unknown"),
      .target_ref = std::string("unknown"),
      .actor_ref = std::string("unknown"),
  };

  assert_true(unknown_fallback.has_required_fields(),
              "policy query context should allow explicit unknown sentinels instead of empty semantic drift");

  const PolicyQueryContext missing_target{
      .module = std::string("plugin"),
      .operation = std::string("load"),
      .target_type = std::string("manifest"),
      .target_ref = std::string(),
      .actor_ref = std::string("system"),
  };

  assert_true(!missing_target.has_required_fields(),
              "policy query context should still reject empty required routing fields");
}

void test_policy_decision_ref_validates_only_frozen_decision_payload() {
  using dasall::infra::policy::PolicyDecision;
  using dasall::infra::policy::PolicyDecisionRef;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(PolicyDecisionRef{}.generation), std::uint64_t>);

  const PolicyDecisionRef deny_decision{
      .decision = PolicyDecision::Deny,
      .reason_code = std::string("plugin_signature_required"),
      .matched_rule_ids = {"deny-unsigned-plugin"},
      .snapshot_id = std::string("snapshot-004"),
      .generation = 4,
      .evidence_ref = std::string("audit:policy-004"),
      .warnings = {"compatibility_mode"},
  };

  assert_true(deny_decision.is_valid(),
              "policy decision ref should remain valid for the frozen decision payload");

  const PolicyDecisionRef unspecified_decision{
      .decision = PolicyDecision::Unspecified,
      .reason_code = std::string("missing-decision"),
      .matched_rule_ids = {"deny-unsigned-plugin"},
      .snapshot_id = std::string("snapshot-004"),
      .generation = 4,
      .evidence_ref = std::string("audit:policy-004"),
      .warnings = {},
  };

  assert_true(!unspecified_decision.is_valid(),
              "policy decision ref should reject unspecified decision semantics");

  const PolicyDecisionRef missing_evidence{
      .decision = PolicyDecision::Allow,
      .reason_code = std::string("plugin_allowed"),
      .matched_rule_ids = {"allow-signed-plugin"},
      .snapshot_id = std::string("snapshot-004"),
      .generation = 4,
      .evidence_ref = std::string(),
      .warnings = {},
  };

  assert_true(!missing_evidence.is_valid(),
              "policy decision ref should reject payloads that lose the evidence anchor");
}

}  // namespace

int main() {
  try {
    test_policy_query_context_keeps_unknown_defaults_without_empty_drift();
    test_policy_decision_ref_validates_only_frozen_decision_payload();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}