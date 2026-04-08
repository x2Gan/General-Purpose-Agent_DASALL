#include <algorithm>
#include <exception>
#include <iostream>
#include <memory>
#include <string>

#include "audit/AuditService.h"
#include "plugin/IPluginPolicyGate.h"
#include "plugin/PluginAuditAdapter.h"
#include "plugin/PluginErrorCode.h"
#include "plugin/PluginLifecycleManager.h"
#include "plugin/PluginValidationPipeline.h"
#include "policy/ISecurityPolicyManager.h"
#include "support/TestAssertions.h"

namespace {

[[nodiscard]] bool has_side_effect(const dasall::infra::AuditEvent& event,
                                   const std::string& expected) {
  return std::find(event.side_effects.begin(),
                   event.side_effects.end(),
                   expected) != event.side_effects.end();
}

[[nodiscard]] dasall::infra::plugin::PluginValidationRequest make_request() {
  return dasall::infra::plugin::PluginValidationRequest{
      .plugin_id = std::string("plugin.echo"),
      .manifest_ref = std::string("manifest:plugin.echo@1"),
      .package_ref = std::string("package:plugin.echo@1"),
      .profile_id = std::string("desktop_full"),
  };
}

[[nodiscard]] dasall::infra::plugin::PluginLoadOptions make_load_options() {
  return dasall::infra::plugin::PluginLoadOptions{
      .profile_id = std::string("desktop_full"),
      .actor_ref = std::string("runtime"),
      .binary_path = std::string("./plugins/plugin.echo.so"),
      .entry_symbol = std::string("plugin_entry"),
      .sandbox_hint = std::string("seccomp:basic"),
      .timeout_ms = 3000,
      .audit_required = true,
      .dry_run = false,
  };
}

[[nodiscard]] dasall::infra::policy::PolicySnapshot make_snapshot() {
  return dasall::infra::policy::PolicySnapshot{
      .snapshot_id = std::string("policy-snapshot-012"),
      .generation = 12,
      .version = std::string("2026.04.07"),
      .mode = dasall::infra::policy::PolicyMode::Enforced,
      .effective_rules = {dasall::infra::policy::PolicyRuleDescriptor{
          .rule_id = std::string("plugin-allow-012"),
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
      .created_at = std::string("2026-04-07T10:00:00Z"),
      .source_chain = {std::string("defaults"), std::string("profile:desktop_full")},
      .last_known_good_ref = std::string("policy-snapshot-011"),
  };
}

[[nodiscard]] dasall::infra::policy::PolicyDecisionRef make_decision(
    dasall::infra::policy::PolicyDecision decision,
    std::string reason_code,
    std::string evidence_ref) {
  return dasall::infra::policy::PolicyDecisionRef{
      .decision = decision,
      .reason_code = std::move(reason_code),
      .matched_rule_ids = {std::string("plugin-allow-012")},
      .snapshot_id = std::string("policy-snapshot-012"),
      .generation = 12,
      .evidence_ref = std::move(evidence_ref),
      .warnings = {},
  };
}

[[nodiscard]] dasall::infra::ExportQuery make_export_query(std::string action) {
  return dasall::infra::ExportQuery{
      .start_ts = 1,
      .end_ts = 4102444800000,
      .actor = std::string(),
      .action = std::move(action),
      .target = std::string(),
      .outcome = dasall::infra::AuditOutcome::Unspecified,
      .page_token = std::string(),
  };
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
      const dasall::infra::plugin::PluginPolicyRequest&,
      const dasall::infra::policy::PolicySnapshot&) const override {
    return decision_;
  }

 private:
  dasall::infra::policy::PolicyDecisionRef decision_;
};

void test_plugin_failure_observability_integration_records_signature_failure_audit() {
  using dasall::infra::AuditOutcome;
  using dasall::infra::audit::AuditService;
  using dasall::infra::audit::AuditServiceConfig;
  using dasall::infra::plugin::PluginAuditAdapter;
  using dasall::infra::plugin::PluginErrorCode;
  using dasall::infra::plugin::PluginValidationPipeline;
  using dasall::infra::plugin::PluginValidationStageResult;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto audit_service = std::make_shared<AuditService>();
  assert_true(audit_service->init(AuditServiceConfig{.primary_capacity = 4,
                                                     .fallback_capacity = 2})
                  .ok,
              "plugin failure observability integration should initialize AuditService before validating failure audits");
  assert_true(audit_service->start().ok,
              "plugin failure observability integration should start AuditService before validating failure audits");

  PluginAuditAdapter audit_adapter(audit_service);
  StaticSecurityPolicyManager policy_manager(make_snapshot());
  ScriptedPluginPolicyGate policy_gate(make_decision(
      dasall::infra::policy::PolicyDecision::Allow,
      std::string("plugin_allowed"),
      std::string("policy://plugin/allow")));
  PluginValidationPipeline pipeline(
      &policy_manager,
      &policy_gate,
      [](const dasall::infra::plugin::PluginValidationRequest& request) {
        return PluginValidationStageResult::failure(
            PluginErrorCode::SignatureFail,
            std::string("report://signature/") + request.plugin_id,
            std::string("evidence://signature/") + request.plugin_id,
            std::string("plugin_signature_failed"),
            std::string("INF_E_PLUGIN_SIGNATURE_FAIL: plugin signature skeleton rejected request"));
      },
      [](const dasall::infra::plugin::PluginValidationRequest& request) {
        return PluginValidationStageResult::success(
            std::string("report://compat/") + request.plugin_id,
            std::string("evidence://compat/") + request.plugin_id,
            std::string("plugin_compatibility_passed"));
      },
      &audit_adapter,
      std::string("runtime"));

  const auto result = pipeline.validate(make_request());
  const auto export_result =
      audit_service->export_audit(make_export_query(std::string("plugin.signature_fail")));

  assert_true(!result.accepted &&
                  result.result_code ==
                      dasall::infra::plugin::map_plugin_error_code(PluginErrorCode::SignatureFail)
                          .result_code &&
                  !result.signature_report_ref.empty() &&
                  result.compatibility_report_ref.empty(),
              "plugin failure observability integration should keep signature failures traceable through the frozen validation result");
  assert_equal(1,
               static_cast<int>(export_result.records.size()),
               "plugin failure observability integration should export one plugin.signature_fail audit event");

  const auto& event = export_result.records.front();
  assert_true(event.outcome == AuditOutcome::Rejected &&
                  event.evidence_ref.ref == result.evidence_ref &&
                  has_side_effect(event, "reason_code:plugin_signature_failed") &&
                  has_side_effect(event, "result_code:ValidationFieldMissing"),
              "plugin failure observability integration should record signature failure action, rejected outcome, and traceable audit facts");
}

void test_plugin_failure_observability_integration_records_compatibility_failure_audit() {
  using dasall::infra::AuditOutcome;
  using dasall::infra::audit::AuditService;
  using dasall::infra::audit::AuditServiceConfig;
  using dasall::infra::plugin::PluginAuditAdapter;
  using dasall::infra::plugin::PluginErrorCode;
  using dasall::infra::plugin::PluginValidationPipeline;
  using dasall::infra::plugin::PluginValidationStageResult;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto audit_service = std::make_shared<AuditService>();
  assert_true(audit_service->init(AuditServiceConfig{.primary_capacity = 4,
                                                     .fallback_capacity = 2})
                  .ok,
              "plugin failure observability integration should initialize AuditService before validating compatibility failure audits");
  assert_true(audit_service->start().ok,
              "plugin failure observability integration should start AuditService before validating compatibility failure audits");

  PluginAuditAdapter audit_adapter(audit_service);
  StaticSecurityPolicyManager policy_manager(make_snapshot());
  ScriptedPluginPolicyGate policy_gate(make_decision(
      dasall::infra::policy::PolicyDecision::Allow,
      std::string("plugin_allowed"),
      std::string("policy://plugin/allow")));
  PluginValidationPipeline pipeline(
      &policy_manager,
      &policy_gate,
      [](const dasall::infra::plugin::PluginValidationRequest& request) {
        return PluginValidationStageResult::success(
            std::string("report://signature/") + request.plugin_id,
            std::string("evidence://signature/") + request.plugin_id,
            std::string("plugin_signature_passed"));
      },
      [](const dasall::infra::plugin::PluginValidationRequest& request) {
        return PluginValidationStageResult::failure(
            PluginErrorCode::CompatibilityFail,
            std::string("report://compat/") + request.plugin_id,
            std::string("evidence://compat/") + request.plugin_id,
            std::string("plugin_abi_incompatible"),
            std::string("INF_E_PLUGIN_COMPATIBILITY_FAIL: plugin compatibility skeleton rejected request"));
      },
      &audit_adapter,
      std::string("runtime"));

  const auto result = pipeline.validate(make_request());
  const auto export_result = audit_service->export_audit(
      make_export_query(std::string("plugin.compatibility_fail")));

  assert_true(!result.accepted &&
                  result.result_code ==
                      dasall::infra::plugin::map_plugin_error_code(
                          PluginErrorCode::CompatibilityFail)
                          .result_code &&
                  !result.signature_report_ref.empty() &&
                  !result.compatibility_report_ref.empty(),
              "plugin failure observability integration should keep compatibility failures traceable through the frozen validation result");
  assert_equal(1,
               static_cast<int>(export_result.records.size()),
               "plugin failure observability integration should export one plugin.compatibility_fail audit event");

  const auto& event = export_result.records.front();
  assert_true(event.outcome == AuditOutcome::Rejected &&
                  event.evidence_ref.ref == result.evidence_ref &&
                  has_side_effect(event, "reason_code:plugin_abi_incompatible") &&
                  has_side_effect(event, "result_code:ValidationFieldMissing"),
              "plugin failure observability integration should record compatibility failure action, rejected outcome, and traceable audit facts");
}

void test_plugin_failure_observability_integration_records_load_timeout_audit() {
  using dasall::contracts::ResultCode;
  using dasall::infra::AuditOutcome;
  using dasall::infra::audit::AuditService;
  using dasall::infra::audit::AuditServiceConfig;
  using dasall::infra::plugin::PluginAuditAdapter;
  using dasall::infra::plugin::PluginLifecycleManager;
  using dasall::infra::plugin::PluginRuntimeLoadResult;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto audit_service = std::make_shared<AuditService>();
  assert_true(audit_service->init(AuditServiceConfig{.primary_capacity = 4,
                                                     .fallback_capacity = 2})
                  .ok,
              "plugin failure observability integration should initialize AuditService before validating load timeout audits");
  assert_true(audit_service->start().ok,
              "plugin failure observability integration should start AuditService before validating load timeout audits");

  PluginAuditAdapter audit_adapter(audit_service);
  PluginLifecycleManager manager(
      [](std::string_view plugin_id,
         const dasall::infra::plugin::PluginLoadOptions&) {
        return PluginRuntimeLoadResult::failure(
            ResultCode::ProviderTimeout,
            std::string("plugin_load_timeout"),
            std::string("evidence://load-timeout/") +
                dasall::infra::plugin::plugin_value_or_unknown(plugin_id),
            std::string("plugin runtime load timed out"));
      },
      {},
      &audit_adapter);

  const auto result = manager.load("plugin.echo", make_load_options());
  const auto export_result =
      audit_service->export_audit(make_export_query(std::string("plugin.load")));

  assert_true(!result.loaded && result.result_code == ResultCode::ProviderTimeout &&
                  result.evidence_ref == "evidence://load-timeout/plugin.echo",
              "plugin failure observability integration should surface load timeout failures through the frozen load result boundary");
  assert_equal(1,
               static_cast<int>(export_result.records.size()),
               "plugin failure observability integration should export one plugin.load audit event for the timeout path");

  const auto& event = export_result.records.front();
  assert_true(event.outcome == AuditOutcome::Failed &&
                  event.evidence_ref.ref == result.evidence_ref &&
                  has_side_effect(event, "reason_code:plugin_load_timeout") &&
                  has_side_effect(event, "result_code:ProviderTimeout"),
              "plugin failure observability integration should record timeout failure action, failed outcome, and stable timeout evidence");
}

}  // namespace

int main() {
  try {
    test_plugin_failure_observability_integration_records_signature_failure_audit();
    test_plugin_failure_observability_integration_records_compatibility_failure_audit();
    test_plugin_failure_observability_integration_records_load_timeout_audit();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}