#include <exception>
#include <iostream>
#include <optional>
#include <string_view>

#include "boundary/PolicyDecisionMappingCatalog.h"
#include "policy/PolicyDecisionRef.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

constexpr std::optional<dasall::contracts::SharedPolicyDecisionSemantic>
to_shared_policy_decision_semantic(dasall::infra::policy::PolicyDecision decision) {
  using dasall::contracts::SharedPolicyDecisionSemantic;
  using dasall::infra::policy::PolicyDecision;

  switch (decision) {
    case PolicyDecision::Allow:
      return SharedPolicyDecisionSemantic::Allow;
    case PolicyDecision::Deny:
      return SharedPolicyDecisionSemantic::Deny;
    case PolicyDecision::RequireConfirmation:
      return SharedPolicyDecisionSemantic::RequireConfirmation;
    case PolicyDecision::Unspecified:
      break;
  }

  return std::nullopt;
}

void test_policy_decision_mapping_catalog_covers_only_shareable_semantics() {
  using dasall::contracts::SharedPolicyDecisionSemantic;
  using dasall::contracts::validate_policy_decision_mapping_catalog;
  using dasall::infra::policy::PolicyDecision;
  using dasall::tests::support::assert_true;

  const auto validation = validate_policy_decision_mapping_catalog();
  assert_true(validation.ok,
              "policy decision mapping catalog must remain complete before PolicyDecisionRef is treated as a formal replacement");

  assert_true(to_shared_policy_decision_semantic(PolicyDecision::Allow).has_value(),
              "allow must remain a shareable PolicyDecision semantic");
  assert_true(to_shared_policy_decision_semantic(PolicyDecision::Deny).has_value(),
              "deny must remain a shareable PolicyDecision semantic");
  assert_true(to_shared_policy_decision_semantic(PolicyDecision::RequireConfirmation).has_value(),
              "require_confirmation must remain a shareable PolicyDecision semantic");
  assert_true(!to_shared_policy_decision_semantic(PolicyDecision::Unspecified).has_value(),
              "unspecified must not be treated as a shareable PolicyDecision semantic");
}

void test_policy_decision_mapping_labels_stay_aligned_with_contract_terms() {
  using dasall::contracts::SharedPolicyDecisionSemantic;
  using dasall::contracts::find_policy_decision_semantic_mapping;
  using dasall::contracts::shared_policy_decision_semantic_name;
  using dasall::tests::support::assert_true;

  const auto* allow_entry =
      find_policy_decision_semantic_mapping(SharedPolicyDecisionSemantic::Allow);
  const auto* deny_entry =
      find_policy_decision_semantic_mapping(SharedPolicyDecisionSemantic::Deny);
  const auto* require_confirmation_entry = find_policy_decision_semantic_mapping(
      SharedPolicyDecisionSemantic::RequireConfirmation);

  assert_true(allow_entry != nullptr && allow_entry->contracts_object_name == std::string_view("PolicyDecision") &&
                  shared_policy_decision_semantic_name(SharedPolicyDecisionSemantic::Allow) ==
                      std::string_view("allow"),
              "allow mapping must stay anchored to the PolicyDecision contract term");
  assert_true(deny_entry != nullptr && deny_entry->contracts_object_name == std::string_view("PolicyDecision") &&
                  shared_policy_decision_semantic_name(SharedPolicyDecisionSemantic::Deny) ==
                      std::string_view("deny"),
              "deny mapping must stay anchored to the PolicyDecision contract term");
  assert_true(require_confirmation_entry != nullptr &&
                  require_confirmation_entry->contracts_object_name ==
                      std::string_view("PolicyDecision") &&
                  shared_policy_decision_semantic_name(
                      SharedPolicyDecisionSemantic::RequireConfirmation) ==
                      std::string_view("require_confirmation"),
              "require_confirmation mapping must stay anchored to the PolicyDecision contract term");
}

void test_policy_decision_ref_trace_fields_remain_infra_private() {
  using dasall::contracts::is_infra_private_policy_decision_ref_field;
  using dasall::tests::support::assert_true;

  assert_true(is_infra_private_policy_decision_ref_field("reason_code"),
              "reason_code must remain an infra-private trace field while shared PolicyDecision is absent");
  assert_true(is_infra_private_policy_decision_ref_field("matched_rule_ids"),
              "matched_rule_ids must remain infra-private explainability data");
  assert_true(is_infra_private_policy_decision_ref_field("snapshot_id"),
              "snapshot_id must remain an infra-private snapshot anchor");
  assert_true(is_infra_private_policy_decision_ref_field("generation"),
              "generation must remain an infra-private snapshot anchor");
  assert_true(is_infra_private_policy_decision_ref_field("evidence_ref"),
              "evidence_ref must remain an infra-private audit anchor");
  assert_true(is_infra_private_policy_decision_ref_field("warnings"),
              "warnings must remain infra-private advisory data");
  assert_true(!is_infra_private_policy_decision_ref_field("decision"),
              "decision itself is the shared semantic anchor and must not be marked as a private trace field");
}

}  // namespace

int main() {
  try {
    test_policy_decision_mapping_catalog_covers_only_shareable_semantics();
    test_policy_decision_mapping_labels_stay_aligned_with_contract_terms();
    test_policy_decision_ref_trace_fields_remain_infra_private();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}