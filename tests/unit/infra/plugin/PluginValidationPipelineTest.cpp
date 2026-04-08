#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <utility>

#include "plugin/PluginValidationPipeline.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

dasall::infra::plugin::PluginValidationRequest make_request() {
  return dasall::infra::plugin::PluginValidationRequest{
      .plugin_id = std::string("plugin.echo"),
      .manifest_ref = std::string("manifest:plugin.echo@1"),
      .package_ref = std::string("package:plugin.echo@1"),
      .profile_id = std::string("desktop_full"),
  };
}

dasall::infra::policy::PolicySnapshot make_snapshot() {
  return dasall::infra::policy::PolicySnapshot{
      .snapshot_id = std::string("policy-snapshot-011"),
      .generation = 11,
      .version = std::string("2026.04.07"),
      .mode = dasall::infra::policy::PolicyMode::Enforced,
      .effective_rules = {dasall::infra::policy::PolicyRuleDescriptor{
          .rule_id = std::string("plugin-allow-011"),
          .domain = dasall::infra::policy::PolicyDomain::PluginLoad,
          .subject = std::string("plugin"),
          .action = std::string("load"),
          .target_selector = std::string("plugin.echo"),
          .effect = dasall::infra::policy::PolicyEffect::Allow,
          .priority = 10,
          .mode = dasall::infra::policy::PolicyMode::Enforced,
          .conditions = {std::string("profile=desktop_full")},
          .reason_code = std::string("plugin_allowed"),
      }},
      .created_at = std::string("2026-04-07T08:00:00Z"),
      .source_chain = {std::string("defaults"), std::string("profile:desktop_full")},
      .last_known_good_ref = std::string("policy-snapshot-010"),
  };
}

dasall::infra::policy::PolicyDecisionRef make_decision(
    dasall::infra::policy::PolicyDecision decision,
    std::string reason_code,
    std::string evidence_ref) {
  return dasall::infra::policy::PolicyDecisionRef{
      .decision = decision,
      .reason_code = std::move(reason_code),
      .matched_rule_ids = {std::string("plugin-allow-011")},
      .snapshot_id = std::string("policy-snapshot-011"),
      .generation = 11,
      .evidence_ref = std::move(evidence_ref),
      .warnings = {},
  };
}

dasall::infra::plugin::SignatureReport make_signature_pass_report(
    const std::string& plugin_id,
    std::string evidence_ref,
    std::string reason_code = "plugin_signature_passed") {
  return dasall::infra::plugin::SignatureReport::success(
      std::string("signer:") + plugin_id,
      std::string("ed25519"),
      dasall::infra::plugin::PluginTrustLevel::Vendor,
      std::move(evidence_ref),
      std::move(reason_code));
}

dasall::infra::plugin::SignatureReport make_signature_fail_report(
    const std::string& plugin_id,
    std::string evidence_ref) {
  return dasall::infra::plugin::SignatureReport::failure(
      std::string("ed25519"),
      dasall::infra::plugin::PluginSignatureChainStatus::SignatureInvalid,
      dasall::infra::plugin::PluginTrustLevel::Vendor,
      std::string("plugin_signature_failed"),
      std::move(evidence_ref),
      std::string("signer:") + plugin_id);
}

dasall::infra::plugin::CompatibilityReport make_compatibility_fail_report(
    std::string evidence_ref) {
  return dasall::infra::plugin::CompatibilityReport::failure(
      false,
      true,
      false,
      {std::string("plugin_abi_incompatible")},
      std::string("x86_64-linux-gnu"),
      std::string("x86_64-linux-gnu@1.2.0"),
      std::move(evidence_ref));
}

class StaticSecurityPolicyManager final : public dasall::infra::policy::ISecurityPolicyManager {
 public:
  explicit StaticSecurityPolicyManager(dasall::infra::policy::PolicySnapshot snapshot)
      : snapshot_(std::move(snapshot)) {}

  [[nodiscard]] dasall::infra::policy::PolicyOpResult load_policy(
      const dasall::infra::policy::PolicyBundle&) override {
    return dasall::infra::policy::PolicyOpResult::success(snapshot_.snapshot_id,
                                                          snapshot_.generation);
  }

  [[nodiscard]] dasall::infra::policy::PolicyOpResult apply_patch(
      const dasall::infra::policy::PolicyPatch&) override {
    return dasall::infra::policy::PolicyOpResult::success(snapshot_.snapshot_id,
                                                          snapshot_.generation);
  }

  [[nodiscard]] dasall::infra::policy::ValidationReport dry_run_patch(
      const dasall::infra::policy::PolicyPatch&) override {
    return {};
  }

  [[nodiscard]] dasall::infra::policy::PolicySnapshot snapshot() const override {
    return snapshot_;
  }

  [[nodiscard]] dasall::infra::policy::PolicyOpResult rollback(
      const std::string& snapshot_id) override {
    return dasall::infra::policy::PolicyOpResult::success(snapshot_id,
                                                          snapshot_.generation,
                                                          true);
  }

  [[nodiscard]] dasall::infra::policy::PolicyDecisionRef evaluate(
      const dasall::infra::policy::PolicyQueryContext&) const override {
    return make_decision(dasall::infra::policy::PolicyDecision::Allow,
                         std::string("plugin_allowed"),
                         std::string("policy://plugin/allow"));
  }

 private:
  dasall::infra::policy::PolicySnapshot snapshot_;
};

class ScriptedPluginPolicyGate final : public dasall::infra::plugin::IPluginPolicyGate {
 public:
  explicit ScriptedPluginPolicyGate(dasall::infra::policy::PolicyDecisionRef decision)
      : decision_(std::move(decision)) {}

  [[nodiscard]] dasall::infra::policy::PolicyDecisionRef evaluate(
      const dasall::infra::plugin::PluginPolicyRequest& request,
      const dasall::infra::policy::PolicySnapshot& policy_snapshot) const override {
    ++call_count_;
    last_request_valid_ = request.is_valid();
    last_snapshot_valid_ = policy_snapshot.is_valid();
    return decision_;
  }

  [[nodiscard]] int call_count() const {
    return call_count_;
  }

  [[nodiscard]] bool last_request_valid() const {
    return last_request_valid_;
  }

  [[nodiscard]] bool last_snapshot_valid() const {
    return last_snapshot_valid_;
  }

 private:
  dasall::infra::policy::PolicyDecisionRef decision_;
  mutable int call_count_ = 0;
  mutable bool last_request_valid_ = false;
  mutable bool last_snapshot_valid_ = false;
};

void test_plugin_validation_pipeline_short_circuits_policy_denials() {
  using dasall::contracts::ResultCode;
  using dasall::infra::plugin::PluginValidationPipeline;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  StaticSecurityPolicyManager policy_manager(make_snapshot());
  ScriptedPluginPolicyGate policy_gate(make_decision(
      dasall::infra::policy::PolicyDecision::Deny,
      std::string("plugin_denied_by_profile"),
      std::string("policy://plugin/deny")));
  int signature_calls = 0;
  int compatibility_calls = 0;
  PluginValidationPipeline pipeline(
      &policy_manager,
      &policy_gate,
      [&signature_calls](const dasall::infra::plugin::PluginValidationRequest& request) {
        ++signature_calls;
        return dasall::infra::plugin::PluginValidationStageResult::success(
            std::string("report://signature/") + request.plugin_id,
            std::string("evidence://signature/") + request.plugin_id,
            std::string("signature_pass"));
      },
      [&compatibility_calls](const dasall::infra::plugin::PluginValidationRequest& request) {
        ++compatibility_calls;
        return dasall::infra::plugin::PluginValidationStageResult::success(
            std::string("report://compat/") + request.plugin_id,
            std::string("evidence://compat/") + request.plugin_id,
            std::string("compat_pass"));
      });

  const auto result = pipeline.validate(make_request());

  assert_true(!result.accepted && result.result_code == ResultCode::PolicyDenied,
              "PluginValidationPipeline should surface policy gate denials as explicit validation rejections");
  assert_true(result.policy_decision.is_valid() &&
                  result.policy_decision.decision == dasall::infra::policy::PolicyDecision::Deny,
              "PluginValidationPipeline should preserve the denying PolicyDecisionRef for traceability");
  assert_equal(0, signature_calls,
               "PluginValidationPipeline should not enter the signature stage after a policy denial");
  assert_equal(0, compatibility_calls,
               "PluginValidationPipeline should not enter the compatibility stage after a policy denial");
  assert_true(!result.signature_report.has_value() && !result.compatibility_report.has_value(),
              "PluginValidationPipeline should not fabricate shared report objects when the policy gate denies before downstream stages run");
  assert_true(policy_gate.call_count() == 1 && policy_gate.last_request_valid() &&
                  policy_gate.last_snapshot_valid(),
              "PluginValidationPipeline should drive IPluginPolicyGate with a valid minimal request and snapshot");
}

void test_plugin_validation_pipeline_returns_signature_report_on_signature_failure() {
  using dasall::contracts::ResultCode;
  using dasall::infra::plugin::PluginErrorCode;
  using dasall::infra::plugin::PluginValidationPipeline;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  StaticSecurityPolicyManager policy_manager(make_snapshot());
  ScriptedPluginPolicyGate policy_gate(make_decision(
      dasall::infra::policy::PolicyDecision::Allow,
      std::string("plugin_allowed"),
      std::string("policy://plugin/allow")));
  int compatibility_calls = 0;
  PluginValidationPipeline pipeline(
      &policy_manager,
      &policy_gate,
      [](const dasall::infra::plugin::PluginValidationRequest& request) {
        const auto evidence_ref = std::string("evidence://signature/") + request.plugin_id;
        return dasall::infra::plugin::PluginValidationStageResult::failure(
            PluginErrorCode::SignatureFail,
            std::string("report://signature/") + request.plugin_id,
            evidence_ref,
            std::string("plugin_signature_failed"),
            std::string("INF_E_PLUGIN_SIGNATURE_FAIL: plugin signature skeleton rejected request"),
            make_signature_fail_report(request.plugin_id, evidence_ref));
      },
      [&compatibility_calls](const dasall::infra::plugin::PluginValidationRequest& request) {
        ++compatibility_calls;
        return dasall::infra::plugin::PluginValidationStageResult::success(
            std::string("report://compat/") + request.plugin_id,
            std::string("evidence://compat/") + request.plugin_id,
            std::string("compat_pass"));
      });

  const auto result = pipeline.validate(make_request());

  assert_true(!result.accepted && result.result_code == ResultCode::ValidationFieldMissing,
              "PluginValidationPipeline should map signature stage failures to the frozen validation error category");
  assert_true(result.policy_decision.is_valid() &&
                  result.policy_decision.decision == dasall::infra::policy::PolicyDecision::Allow,
              "PluginValidationPipeline should preserve the passing policy decision when the signature stage fails later");
    assert_true(!result.signature_report_ref.empty() && result.compatibility_report_ref.empty() &&
            result.signature_report.has_value() &&
            result.signature_report->chain_status ==
              dasall::infra::plugin::PluginSignatureChainStatus::SignatureInvalid &&
            !result.compatibility_report.has_value(),
          "PluginValidationPipeline should surface the failing shared signature report object together with its ref while keeping compatibility empty on signature rejection");
  assert_equal(0, compatibility_calls,
               "PluginValidationPipeline should stop before the compatibility stage when signature verification fails");
  assert_true(result.references_only_contract_error_types(),
              "PluginValidationPipeline signature failures should remain inside contracts ResultCode/ErrorInfo types");
}

void test_plugin_validation_pipeline_returns_compatibility_report_on_compatibility_failure() {
  using dasall::contracts::ResultCode;
  using dasall::infra::plugin::PluginErrorCode;
  using dasall::infra::plugin::PluginValidationPipeline;
  using dasall::tests::support::assert_true;

  StaticSecurityPolicyManager policy_manager(make_snapshot());
  ScriptedPluginPolicyGate policy_gate(make_decision(
      dasall::infra::policy::PolicyDecision::Allow,
      std::string("plugin_allowed"),
      std::string("policy://plugin/allow")));
  PluginValidationPipeline pipeline(
      &policy_manager,
      &policy_gate,
      [](const dasall::infra::plugin::PluginValidationRequest& request) {
      const auto evidence_ref = std::string("evidence://signature/") + request.plugin_id;
        return dasall::infra::plugin::PluginValidationStageResult::success(
            std::string("report://signature/") + request.plugin_id,
        evidence_ref,
        std::string("plugin_signature_passed"),
        make_signature_pass_report(request.plugin_id, evidence_ref));
      },
      [](const dasall::infra::plugin::PluginValidationRequest& request) {
      const auto evidence_ref = std::string("evidence://compat/") + request.plugin_id;
        return dasall::infra::plugin::PluginValidationStageResult::failure(
            PluginErrorCode::CompatibilityFail,
            std::string("report://compat/") + request.plugin_id,
        evidence_ref,
            std::string("plugin_abi_incompatible"),
        std::string("INF_E_PLUGIN_COMPATIBILITY_FAIL: plugin compatibility skeleton rejected request"),
        std::nullopt,
        make_compatibility_fail_report(evidence_ref));
      });

  const auto result = pipeline.validate(make_request());

  assert_true(!result.accepted && result.result_code == ResultCode::ValidationFieldMissing,
              "PluginValidationPipeline should map compatibility stage failures to the frozen validation error category");
  assert_true(!result.signature_report_ref.empty() && !result.compatibility_report_ref.empty() &&
                  result.signature_report.has_value() &&
                  result.compatibility_report.has_value() &&
                  !result.compatibility_report->abi_ok &&
                  !result.compatibility_report->dependency_ok,
              "PluginValidationPipeline should preserve both shared report objects and their refs when compatibility fails after signature passes");
  assert_true(result.references_only_contract_error_types(),
              "PluginValidationPipeline compatibility failures should remain inside contracts ResultCode/ErrorInfo types");
}

void test_plugin_validation_pipeline_accepts_when_all_three_stages_pass() {
  using dasall::infra::plugin::PluginValidationPipeline;
  using dasall::tests::support::assert_true;

  StaticSecurityPolicyManager policy_manager(make_snapshot());
  ScriptedPluginPolicyGate policy_gate(make_decision(
      dasall::infra::policy::PolicyDecision::Allow,
      std::string("plugin_allowed"),
      std::string("policy://plugin/allow")));
  PluginValidationPipeline pipeline(&policy_manager, &policy_gate);

  const auto result = pipeline.validate(make_request());

  assert_true(result.accepted && result.has_traceable_refs() &&
                  result.signature_report.has_value() &&
                  result.signature_report->verified &&
                  result.compatibility_report.has_value() &&
                  result.compatibility_report->abi_ok &&
                  result.compatibility_report->api_ok &&
                  result.compatibility_report->dependency_ok,
              "PluginValidationPipeline should aggregate shared signature and compatibility report objects together with traceable refs when all stages pass");
}

}  // namespace

int main() {
  try {
    test_plugin_validation_pipeline_short_circuits_policy_denials();
    test_plugin_validation_pipeline_returns_signature_report_on_signature_failure();
    test_plugin_validation_pipeline_returns_compatibility_report_on_compatibility_failure();
    test_plugin_validation_pipeline_accepts_when_all_three_stages_pass();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}