#include <exception>
#include <iostream>
#include <string>
#include <type_traits>

#include "plugin/IPluginPolicyGate.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

dasall::infra::plugin::PluginDescriptor make_descriptor(dasall::infra::plugin::PluginStatus status) {
  return dasall::infra::plugin::PluginDescriptor::normalize(dasall::infra::plugin::PluginDescriptor{
      .plugin_id = std::string("plugin.echo"),
      .version = std::string("1.0.0"),
      .abi = std::string("linux.gcc13"),
      .trust_level = dasall::infra::plugin::PluginTrustLevel::Internal,
      .status = status,
      .source = std::string("./plugins/plugin.echo"),
  });
}

dasall::infra::policy::PolicySnapshot make_policy_snapshot() {
  return dasall::infra::policy::PolicySnapshot{
      .snapshot_id = std::string("policy-snapshot-010"),
      .generation = 10,
      .version = std::string("2026.04.01"),
      .mode = dasall::infra::policy::PolicyMode::Enforced,
      .effective_rules = {dasall::infra::policy::PolicyRuleDescriptor{
          .rule_id = std::string("plugin-allow-001"),
          .domain = dasall::infra::policy::PolicyDomain::PluginLoad,
          .subject = std::string("runtime"),
          .action = std::string("load"),
          .target_selector = std::string("plugin.echo"),
          .effect = dasall::infra::policy::PolicyEffect::Allow,
          .priority = 0,
          .mode = dasall::infra::policy::PolicyMode::Enforced,
          .conditions = {std::string("profile=desktop_full")},
          .reason_code = std::string("plugin_allowed"),
      }},
      .created_at = std::string("2026-04-01T12:00:00Z"),
      .source_chain = {std::string("defaults"), std::string("profile"), std::string("deployment")},
      .last_known_good_ref = std::string("policy-snapshot-009"),
  };
}

class NullPluginPolicyGate final : public dasall::infra::plugin::IPluginPolicyGate {
 public:
  [[nodiscard]] dasall::infra::policy::PolicyDecisionRef evaluate(
      const dasall::infra::plugin::PluginPolicyRequest& request,
      const dasall::infra::policy::PolicySnapshot& policy_snapshot) const override {
    if (!request.is_valid() || !policy_snapshot.is_valid()) {
      return dasall::infra::policy::PolicyDecisionRef{
          .decision = dasall::infra::policy::PolicyDecision::Deny,
          .reason_code = std::string("plugin_policy_invalid_request"),
          .matched_rule_ids = {std::string("plugin-policy-input-guard")},
          .snapshot_id = policy_snapshot.snapshot_id.empty() ? std::string("unknown") : policy_snapshot.snapshot_id,
          .generation = policy_snapshot.generation == 0 ? 1 : policy_snapshot.generation,
          .evidence_ref = std::string("audit:plugin-policy-invalid-request"),
          .warnings = {},
      };
    }

    return dasall::infra::policy::PolicyDecisionRef{
        .decision = dasall::infra::policy::PolicyDecision::Allow,
        .reason_code = std::string("plugin_policy_allow"),
        .matched_rule_ids = {policy_snapshot.effective_rules.front().rule_id},
        .snapshot_id = policy_snapshot.snapshot_id,
        .generation = policy_snapshot.generation,
        .evidence_ref = std::string("audit:plugin-policy-allow"),
        .warnings = {},
    };
  }
};

void test_plugin_policy_gate_interface_freezes_evaluate_signature() {
  using dasall::infra::plugin::IPluginPolicyGate;
  using dasall::infra::plugin::PluginPolicyRequest;
  using dasall::infra::policy::PolicyDecisionRef;
  using dasall::infra::policy::PolicySnapshot;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(std::declval<const IPluginPolicyGate&>().evaluate(
                                   std::declval<const PluginPolicyRequest&>(),
                                   std::declval<const PolicySnapshot&>())),
                               PolicyDecisionRef>);

  NullPluginPolicyGate gate;
  const auto decision = gate.evaluate(dasall::infra::plugin::PluginPolicyRequest{
                                          .descriptor = make_descriptor(dasall::infra::plugin::PluginStatus::Discovered),
                                          .manifest_ref = std::string("manifest:plugin.echo@1"),
                                          .profile_id = std::string("desktop_full"),
                                      },
                                      make_policy_snapshot());

  assert_true(decision.is_valid(),
              "IPluginPolicyGate should freeze PolicyDecisionRef as the evaluation boundary output");
  assert_true(decision.decision == dasall::infra::policy::PolicyDecision::Allow,
              "policy gate should preserve allow/deny semantics through PolicyDecisionRef");
}

void test_plugin_policy_gate_interface_rejects_invalid_requests_with_traceable_decision_refs() {
  using dasall::tests::support::assert_true;

  NullPluginPolicyGate gate;
  const auto decision = gate.evaluate(dasall::infra::plugin::PluginPolicyRequest{},
                                      dasall::infra::policy::PolicySnapshot{});

  assert_true(decision.is_valid(),
              "IPluginPolicyGate should still emit a traceable PolicyDecisionRef when request or snapshot is invalid");
  assert_true(decision.decision == dasall::infra::policy::PolicyDecision::Deny,
              "invalid policy gate inputs should remain explicit deny decisions rather than implicit success");
}

}  // namespace

int main() {
  try {
    test_plugin_policy_gate_interface_freezes_evaluate_signature();
    test_plugin_policy_gate_interface_rejects_invalid_requests_with_traceable_decision_refs();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}