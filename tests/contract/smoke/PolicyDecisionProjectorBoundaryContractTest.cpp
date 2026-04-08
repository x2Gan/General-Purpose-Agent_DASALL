#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#include "boundary/PolicyDecisionMappingCatalog.h"
#include "policy/PolicyDecisionProjector.h"
#include "support/TestAssertions.h"

namespace {

[[nodiscard]] std::optional<dasall::contracts::SharedPolicyDecisionSemantic>
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

dasall::infra::policy::PolicyRuleDescriptor make_rule(
    std::string rule_id,
    dasall::infra::policy::PolicyEffect effect,
    std::string reason_code) {
  return dasall::infra::policy::PolicyRuleDescriptor{
      .rule_id = std::move(rule_id),
      .domain = dasall::infra::policy::PolicyDomain::PluginLoad,
      .subject = std::string("ops-user"),
      .action = std::string("load"),
      .target_selector = std::string("manifest:plugin.echo"),
      .effect = effect,
      .priority = 3,
      .mode = dasall::infra::policy::PolicyMode::Enforced,
      .conditions = {std::string("policy_enabled=true")},
      .reason_code = std::move(reason_code),
  };
}

dasall::infra::policy::PolicyRuleDescriptor make_default_rule() {
  return dasall::infra::policy::PolicyRuleDescriptor{
      .rule_id = std::string("policy-loader-default-effect"),
      .domain = dasall::infra::policy::PolicyDomain::PolicyAdmin,
      .subject = std::string("infra.policy"),
      .action = std::string("evaluate_default"),
      .target_selector = std::string("policy:default_effect"),
      .effect = dasall::infra::policy::PolicyEffect::Deny,
      .priority = 4,
      .mode = dasall::infra::policy::PolicyMode::Enforced,
      .conditions = {std::string("default_effect=deny")},
      .reason_code = std::string("policy_default_deny"),
  };
}

dasall::infra::policy::PolicySnapshot make_snapshot(
    std::vector<dasall::infra::policy::PolicyRuleDescriptor> rules) {
  return dasall::infra::policy::PolicySnapshot{
      .snapshot_id = std::string("snapshot-projector-boundary-001"),
      .generation = 21,
      .version = std::string("policy-v21"),
      .mode = dasall::infra::policy::PolicyMode::Enforced,
      .effective_rules = std::move(rules),
      .created_at = std::string("2026-04-05T17:15:00Z"),
      .source_chain = {std::string("defaults"), std::string("profile:desktop_full")},
      .last_known_good_ref = std::string("snapshot-projector-boundary-000"),
  };
}

void test_policy_decision_projector_boundary_keeps_decision_semantics_inside_mapping_catalog() {
  using dasall::contracts::find_policy_decision_semantic_mapping;
  using dasall::contracts::validate_policy_decision_mapping_catalog;
  using dasall::infra::policy::PolicyDecisionProjector;
  using dasall::infra::policy::PolicyEffect;
  using dasall::infra::policy::PolicyQueryContext;
  using dasall::tests::support::assert_true;

  const auto validation = validate_policy_decision_mapping_catalog();
  assert_true(validation.ok,
              "policy decision mapping catalog must stay complete before projector outputs are treated as boundary-safe references");

  const PolicyDecisionProjector projector;
  const auto decision = projector.project(
      PolicyQueryContext{
          .module = std::string("plugin"),
          .operation = std::string("load"),
          .target_type = std::string("manifest"),
          .target_ref = std::string("plugin.echo"),
          .actor_ref = std::string("ops-user"),
      },
      make_snapshot({
          make_rule("require-plugin-confirmation",
                    PolicyEffect::RequireConfirmation,
                    "plugin_confirmation_required"),
          make_default_rule(),
      }));

  const auto semantic = to_shared_policy_decision_semantic(decision.decision);
  assert_true(decision.is_valid() && semantic.has_value() &&
                  find_policy_decision_semantic_mapping(*semantic) != nullptr,
              "PolicyDecisionProjector should keep decision outputs inside the allow/deny/require_confirmation mapping catalog");
}

void test_policy_decision_projector_boundary_keeps_evidence_ref_as_private_audit_anchor() {
  using dasall::contracts::is_infra_private_policy_decision_ref_field;
  using dasall::infra::policy::PolicyDecision;
  using dasall::infra::policy::PolicyDecisionProjector;
  using dasall::infra::policy::PolicyQueryContext;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(dasall::infra::policy::PolicyDecisionRef{}.evidence_ref),
                               std::string>);

  const PolicyDecisionProjector projector;
  const auto decision = projector.project(
      PolicyQueryContext{
          .module = std::string("diagnostics"),
          .operation = std::string("execute"),
          .target_type = std::string("command"),
          .target_ref = std::string("export_snapshot"),
          .actor_ref = std::string("ops-user"),
      },
      make_snapshot({make_default_rule()}));

  assert_true(is_infra_private_policy_decision_ref_field("evidence_ref") &&
                  decision.is_valid() && decision.decision == PolicyDecision::Deny &&
                  decision.matched_rule_ids.front() == "policy-loader-default-effect" &&
                  decision.evidence_ref.starts_with("audit://policy/decision/") &&
                  decision.warnings.size() == 1 &&
                  decision.warnings.front() == "default_effect_applied",
              "PolicyDecisionProjector should keep evidence_ref as an infra-private audit anchor even on default deny fallback");
}

}  // namespace

int main() {
  try {
    test_policy_decision_projector_boundary_keeps_decision_semantics_inside_mapping_catalog();
    test_policy_decision_projector_boundary_keeps_evidence_ref_as_private_audit_anchor();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}