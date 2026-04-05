#include <exception>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "policy/PolicyDecisionProjector.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

dasall::infra::policy::PolicyRuleDescriptor make_rule(
    std::string rule_id,
    dasall::infra::policy::PolicyDomain domain,
    std::string subject,
    std::string action,
    std::string target_selector,
    dasall::infra::policy::PolicyEffect effect,
    std::uint32_t priority,
    std::string reason_code) {
  return dasall::infra::policy::PolicyRuleDescriptor{
      .rule_id = std::move(rule_id),
      .domain = domain,
      .subject = std::move(subject),
      .action = std::move(action),
      .target_selector = std::move(target_selector),
      .effect = effect,
      .priority = priority,
      .mode = dasall::infra::policy::PolicyMode::Enforced,
      .conditions = {std::string("policy_enabled=true")},
      .reason_code = std::move(reason_code),
  };
}

dasall::infra::policy::PolicyRuleDescriptor make_default_rule(
    dasall::infra::policy::PolicyEffect effect,
    std::string reason_code = "policy_default_deny") {
  return make_rule("policy-loader-default-effect",
                   dasall::infra::policy::PolicyDomain::PolicyAdmin,
                   "infra.policy",
                   "evaluate_default",
                   "policy:default_effect",
                   effect,
                   2,
                   std::move(reason_code));
}

dasall::infra::policy::PolicySnapshot make_snapshot(
    std::vector<dasall::infra::policy::PolicyRuleDescriptor> rules) {
  return dasall::infra::policy::PolicySnapshot{
      .snapshot_id = std::string("snapshot-projector-014"),
      .generation = 14,
      .version = std::string("policy-v14"),
      .mode = dasall::infra::policy::PolicyMode::Enforced,
      .effective_rules = std::move(rules),
      .created_at = std::string("2026-04-05T16:40:00Z"),
      .source_chain = {std::string("defaults"), std::string("profile:desktop_full")},
      .last_known_good_ref = std::string("snapshot-projector-013"),
  };
}

void test_policy_decision_projector_projects_direct_allow_matches() {
  using dasall::infra::policy::PolicyDecision;
  using dasall::infra::policy::PolicyDecisionProjector;
  using dasall::infra::policy::PolicyDomain;
  using dasall::infra::policy::PolicyEffect;
  using dasall::infra::policy::PolicyQueryContext;
  using dasall::tests::support::assert_true;

  const PolicyDecisionProjector projector;
  const auto decision = projector.project(
      PolicyQueryContext{
          .module = std::string("plugin"),
          .operation = std::string("load"),
          .target_type = std::string("plugin"),
          .target_ref = std::string("diagnostics-export"),
          .actor_ref = std::string("ops-user"),
      },
      make_snapshot({
          make_rule("allow-signed-plugin",
                    PolicyDomain::PluginLoad,
                    "ops-user",
                    "load",
                    "plugin:diagnostics-export",
                    PolicyEffect::Allow,
                    5,
                    "plugin_allowed"),
          make_default_rule(PolicyEffect::Deny),
      }));

  assert_true(decision.is_valid() && decision.decision == PolicyDecision::Allow &&
                  decision.reason_code == "plugin_allowed" &&
                  decision.matched_rule_ids.size() == 1 &&
                  decision.matched_rule_ids.front() == "allow-signed-plugin" &&
                  decision.snapshot_id == "snapshot-projector-014" &&
                  decision.generation == 14 &&
                  decision.evidence_ref.find("allow-signed-plugin") != std::string::npos &&
                  decision.warnings.empty(),
              "PolicyDecisionProjector should project a direct exact-match allow rule into a valid PolicyDecisionRef");
}

void test_policy_decision_projector_falls_back_to_default_effect_on_miss() {
  using dasall::infra::policy::PolicyDecision;
  using dasall::infra::policy::PolicyDecisionProjector;
  using dasall::infra::policy::PolicyDomain;
  using dasall::infra::policy::PolicyEffect;
  using dasall::infra::policy::PolicyQueryContext;
  using dasall::tests::support::assert_true;

  const PolicyDecisionProjector projector;
  const auto decision = projector.project(
      PolicyQueryContext{
          .module = std::string("diagnostics"),
          .operation = std::string("execute"),
          .target_type = std::string("command"),
          .target_ref = std::string("export_snapshot"),
          .actor_ref = std::string("ops-user"),
      },
      make_snapshot({
          make_rule("allow-signed-plugin",
                    PolicyDomain::PluginLoad,
                    "ops-user",
                    "load",
                    "plugin:diagnostics-export",
                    PolicyEffect::Allow,
                    5,
                    "plugin_allowed"),
          make_default_rule(PolicyEffect::Deny, "policy_default_deny"),
      }));

  assert_true(decision.is_valid() && decision.decision == PolicyDecision::Deny &&
                  decision.reason_code == "policy_default_deny" &&
                  decision.matched_rule_ids.size() == 1 &&
                  decision.matched_rule_ids.front() == "policy-loader-default-effect" &&
                  decision.evidence_ref.find("policy-loader-default-effect") != std::string::npos &&
                  decision.warnings.size() == 1 &&
                  decision.warnings.front() == "default_effect_applied",
              "PolicyDecisionProjector should fail closed through the default-effect rule when no direct match exists");
}

void test_policy_decision_projector_preserves_require_confirmation_semantics() {
  using dasall::infra::policy::PolicyDecision;
  using dasall::infra::policy::PolicyDecisionProjector;
  using dasall::infra::policy::PolicyDomain;
  using dasall::infra::policy::PolicyEffect;
  using dasall::infra::policy::PolicyQueryContext;
  using dasall::tests::support::assert_true;

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
                    PolicyDomain::PluginLoad,
                    "ops-user",
                    "load",
                    "manifest:plugin.echo",
                    PolicyEffect::RequireConfirmation,
                    2,
                    "plugin_confirmation_required"),
          make_default_rule(PolicyEffect::Deny),
      }));

  assert_true(decision.is_valid() &&
                  decision.decision == PolicyDecision::RequireConfirmation &&
                  decision.reason_code == "plugin_confirmation_required" &&
                  decision.matched_rule_ids.front() == "require-plugin-confirmation",
              "PolicyDecisionProjector should preserve require_confirmation as a first-class decision semantic");
}

void test_policy_decision_projector_prefers_more_specific_deny_matches_before_priority() {
  using dasall::infra::policy::PolicyDecision;
  using dasall::infra::policy::PolicyDecisionProjector;
  using dasall::infra::policy::PolicyDomain;
  using dasall::infra::policy::PolicyEffect;
  using dasall::infra::policy::PolicyQueryContext;
  using dasall::tests::support::assert_true;

  const PolicyDecisionProjector projector;
  const auto decision = projector.project(
      PolicyQueryContext{
          .module = std::string("plugin"),
          .operation = std::string("load"),
          .target_type = std::string("plugin"),
          .target_ref = std::string("diagnostics-export"),
          .actor_ref = std::string("ops-user"),
      },
      make_snapshot({
          make_rule("allow-all-plugin-loads",
                    PolicyDomain::PluginLoad,
                    "ops-user",
                    "load",
                    "plugin:*",
                    PolicyEffect::Allow,
                    1,
                    "plugin_allow_all"),
          make_rule("deny-diagnostics-export",
                    PolicyDomain::PluginLoad,
                    "ops-user",
                    "load",
                    "plugin:diagnostics-export",
                    PolicyEffect::Deny,
                    50,
                    "plugin_denied"),
          make_default_rule(PolicyEffect::Deny),
      }));

  assert_true(decision.is_valid() && decision.decision == PolicyDecision::Deny &&
                  decision.reason_code == "plugin_denied" &&
                  decision.matched_rule_ids.size() == 2 &&
                  decision.matched_rule_ids.front() == "deny-diagnostics-export",
              "PolicyDecisionProjector should honor target-selector specificity before numeric priority when projecting deny results");
}

}  // namespace

int main() {
  try {
    test_policy_decision_projector_projects_direct_allow_matches();
    test_policy_decision_projector_falls_back_to_default_effect_on_miss();
    test_policy_decision_projector_preserves_require_confirmation_semantics();
    test_policy_decision_projector_prefers_more_specific_deny_matches_before_priority();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}