#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <type_traits>

#include "plugin/PluginValidationPipeline.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

template <typename T, typename = void>
struct HasSignatureReportObject : std::false_type {};

template <typename T>
struct HasSignatureReportObject<T,
                                std::void_t<decltype(std::declval<T>().signature_report)>>
    : std::true_type {};

template <typename T, typename = void>
struct HasCompatibilityReportObject : std::false_type {};

template <typename T>
struct HasCompatibilityReportObject<
    T,
    std::void_t<decltype(std::declval<T>().compatibility_report)>> : std::true_type {};

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
      .created_at = std::string("2026-04-07T08:10:00Z"),
      .source_chain = {std::string("defaults"), std::string("profile:desktop_full")},
      .last_known_good_ref = std::string("policy-snapshot-011"),
  };
}

class StaticSecurityPolicyManager final : public dasall::infra::policy::ISecurityPolicyManager {
 public:
  [[nodiscard]] dasall::infra::policy::PolicyOpResult load_policy(
      const dasall::infra::policy::PolicyBundle&) override {
    return dasall::infra::policy::PolicyOpResult::success(std::string("policy-snapshot-012"), 12);
  }

  [[nodiscard]] dasall::infra::policy::PolicyOpResult apply_patch(
      const dasall::infra::policy::PolicyPatch&) override {
    return dasall::infra::policy::PolicyOpResult::success(std::string("policy-snapshot-012"), 12);
  }

  [[nodiscard]] dasall::infra::policy::ValidationReport dry_run_patch(
      const dasall::infra::policy::PolicyPatch&) override {
    return {};
  }

  [[nodiscard]] dasall::infra::policy::PolicySnapshot snapshot() const override {
    return make_snapshot();
  }

  [[nodiscard]] dasall::infra::policy::PolicyOpResult rollback(
      const std::string& snapshot_id) override {
    return dasall::infra::policy::PolicyOpResult::success(snapshot_id, 12, true);
  }

  [[nodiscard]] dasall::infra::policy::PolicyDecisionRef evaluate(
      const dasall::infra::policy::PolicyQueryContext&) const override {
    return {};
  }
};

class StaticPluginPolicyGate final : public dasall::infra::plugin::IPluginPolicyGate {
 public:
  explicit StaticPluginPolicyGate(dasall::infra::policy::PolicyDecision decision)
      : decision_(decision) {}

  [[nodiscard]] dasall::infra::policy::PolicyDecisionRef evaluate(
      const dasall::infra::plugin::PluginPolicyRequest&,
      const dasall::infra::policy::PolicySnapshot&) const override {
    return dasall::infra::policy::PolicyDecisionRef{
        .decision = decision_,
        .reason_code = decision_ == dasall::infra::policy::PolicyDecision::Allow
                           ? std::string("plugin_allowed")
                           : std::string("plugin_denied"),
        .matched_rule_ids = {std::string("plugin-allow-012")},
        .snapshot_id = std::string("policy-snapshot-012"),
        .generation = 12,
        .evidence_ref = decision_ == dasall::infra::policy::PolicyDecision::Allow
                            ? std::string("policy://plugin/allow")
                            : std::string("policy://plugin/deny"),
        .warnings = {},
    };
  }

 private:
  dasall::infra::policy::PolicyDecision decision_;
};

void test_plugin_validation_pipeline_keeps_ref_only_report_boundary_on_stage_failures() {
  using dasall::contracts::ErrorInfo;
  using dasall::contracts::ResultCode;
  using dasall::infra::plugin::PluginErrorCode;
  using dasall::infra::plugin::PluginValidationPipeline;
  using dasall::infra::plugin::PluginValidationResult;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(PluginValidationResult{}.result_code), ResultCode>);
  static_assert(std::is_same_v<decltype(PluginValidationResult{}.error_info), std::optional<ErrorInfo>>);
  static_assert(std::is_same_v<decltype(PluginValidationResult{}.signature_report_ref), std::string>);
  static_assert(std::is_same_v<decltype(PluginValidationResult{}.compatibility_report_ref), std::string>);
  static_assert(!HasSignatureReportObject<PluginValidationResult>::value);
  static_assert(!HasCompatibilityReportObject<PluginValidationResult>::value);

  StaticSecurityPolicyManager policy_manager;
  StaticPluginPolicyGate policy_gate(dasall::infra::policy::PolicyDecision::Allow);
  PluginValidationPipeline pipeline(
      &policy_manager,
      &policy_gate,
      [](const dasall::infra::plugin::PluginValidationRequest& request) {
        return dasall::infra::plugin::PluginValidationStageResult::failure(
            PluginErrorCode::SignatureFail,
            std::string("report://signature/") + request.plugin_id,
            std::string("evidence://signature/") + request.plugin_id,
            std::string("plugin_signature_failed"),
            std::string("INF_E_PLUGIN_SIGNATURE_FAIL: signature validation failed"));
      });

  const auto result = pipeline.validate(make_request());

  assert_true(!result.accepted && result.references_only_contract_error_types(),
              "PluginValidationPipeline should keep stage failure payloads inside contracts ResultCode/ErrorInfo while exposing only report refs");
  assert_true(!result.signature_report_ref.empty() && result.compatibility_report_ref.empty(),
              "PluginValidationPipeline should expose signature failure evidence through ref fields only");
}

void test_plugin_validation_pipeline_keeps_policy_decision_traceable_on_policy_deny() {
  using dasall::contracts::ResultCode;
  using dasall::infra::plugin::PluginValidationPipeline;
  using dasall::tests::support::assert_true;

  StaticSecurityPolicyManager policy_manager;
  StaticPluginPolicyGate policy_gate(dasall::infra::policy::PolicyDecision::Deny);
  PluginValidationPipeline pipeline(&policy_manager, &policy_gate);

  const auto result = pipeline.validate(make_request());

  assert_true(!result.accepted && result.result_code == ResultCode::PolicyDenied,
              "PluginValidationPipeline should preserve policy denials inside the contracts policy category");
  assert_true(result.policy_decision.is_valid() && result.signature_report_ref.empty() &&
                  result.compatibility_report_ref.empty(),
              "PluginValidationPipeline should keep policy denials traceable without fabricating downstream report objects");
}

}  // namespace

int main() {
  try {
    test_plugin_validation_pipeline_keeps_ref_only_report_boundary_on_stage_failures();
    test_plugin_validation_pipeline_keeps_policy_decision_traceable_on_policy_deny();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}