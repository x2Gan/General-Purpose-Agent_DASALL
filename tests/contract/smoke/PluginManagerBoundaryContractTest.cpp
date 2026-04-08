#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <type_traits>

#include "plugin/IPluginManager.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

template <typename T, typename = void>
struct HasSignatureReport : std::false_type {};

template <typename T>
struct HasSignatureReport<T, std::void_t<decltype(std::declval<T>().signature_report)>>
  : std::true_type {};

template <typename T, typename = void>
struct HasCompatibilityReport : std::false_type {};

template <typename T>
struct HasCompatibilityReport<T,
                std::void_t<decltype(std::declval<T>().compatibility_report)>>
  : std::true_type {};

void test_plugin_manager_results_keep_contract_error_types_and_optional_report_objects() {
  using dasall::contracts::ErrorInfo;
  using dasall::contracts::ResultCode;
  using dasall::infra::plugin::ActivePluginSet;
  using dasall::infra::plugin::CompatibilityReport;
  using dasall::infra::plugin::PluginDescriptor;
  using dasall::infra::plugin::PluginLoadResult;
  using dasall::infra::plugin::PluginOperationPhase;
  using dasall::infra::plugin::PluginUnloadResult;
  using dasall::infra::plugin::PluginValidationResult;
  using dasall::infra::plugin::PluginSignatureChainStatus;
  using dasall::infra::plugin::PluginTrustLevel;
  using dasall::infra::plugin::SignatureReport;
  using dasall::infra::policy::PolicyDecisionRef;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(PluginValidationResult{}.result_code), ResultCode>);
  static_assert(std::is_same_v<decltype(PluginLoadResult{}.result_code), ResultCode>);
  static_assert(std::is_same_v<decltype(PluginUnloadResult{}.result_code), ResultCode>);
  static_assert(
      std::is_same_v<decltype(PluginValidationResult{}.error_info), std::optional<ErrorInfo>>);
  static_assert(std::is_same_v<decltype(PluginLoadResult{}.error_info), std::optional<ErrorInfo>>);
  static_assert(
      std::is_same_v<decltype(PluginUnloadResult{}.error_info), std::optional<ErrorInfo>>);
  static_assert(std::is_same_v<decltype(PluginValidationResult{}.policy_decision),
                               PolicyDecisionRef>);
  static_assert(std::is_same_v<decltype(PluginValidationResult{}.signature_report_ref),
                               std::string>);
  static_assert(std::is_same_v<decltype(PluginValidationResult{}.compatibility_report_ref),
                               std::string>);
    static_assert(std::is_same_v<decltype(PluginValidationResult{}.signature_report),
                   std::optional<SignatureReport>>);
    static_assert(std::is_same_v<decltype(PluginValidationResult{}.compatibility_report),
                   std::optional<CompatibilityReport>>);
  static_assert(
      std::is_same_v<decltype(ActivePluginSet{}.active_plugins), std::vector<PluginDescriptor>>);
    static_assert(HasSignatureReport<PluginValidationResult>::value);
    static_assert(HasCompatibilityReport<PluginValidationResult>::value);

    const auto validation_success = PluginValidationResult::success(
      std::string("plugin.echo"),
      PolicyDecisionRef{
        .decision = dasall::infra::policy::PolicyDecision::Allow,
        .reason_code = std::string("plugin_allowed"),
        .matched_rule_ids = {std::string("plugin-allow-012")},
        .snapshot_id = std::string("policy-snapshot-012"),
        .generation = 12,
        .evidence_ref = std::string("policy://plugin/allow"),
        .warnings = {},
      },
      std::string("report://signature/plugin.echo"),
      std::string("report://compat/plugin.echo"),
      std::string("observation:plugin.echo.validate"),
      SignatureReport::success(std::string("signer:plugin.echo"),
                   std::string("ed25519"),
                   PluginTrustLevel::Internal,
                   std::string("evidence://signature/plugin.echo/verified"),
                   std::string("plugin_signature_verified")),
      CompatibilityReport::success(std::string("x86_64-linux-gnu"),
                     std::string("x86_64-linux-gnu@1.2.0"),
                     std::string("evidence://compat/plugin.echo/pass")));
    assert_true(validation_success.has_traceable_refs(),
          "plugin validation successes should carry optional shared report objects without losing traceable ref aggregation");

  const auto validation_failure = PluginValidationResult::failure(
      ResultCode::ValidationFieldMissing,
      std::string("plugin.echo"),
      std::string("plugin validation failed"),
      std::string("plugin.validate"),
      std::string("PluginManagerBoundaryContractTest"),
      std::string("observation:plugin.echo.validate.fail"),
      std::string("report://signature/plugin.echo/fail"),
      {},
      SignatureReport::failure(std::string("ed25519"),
                   PluginSignatureChainStatus::SignatureInvalid,
                   PluginTrustLevel::Vendor,
                   std::string("plugin_signature_failed"),
                   std::string("evidence://signature/plugin.echo/fail"),
                   std::string("signer:plugin.echo")));
    assert_true(validation_failure.references_only_contract_error_types() &&
            validation_failure.has_traceable_refs(),
          "plugin validation failures should stay inside contracts ResultCode/ErrorInfo types while still carrying optional shared report objects behind ref links");

  const auto load_failure = PluginLoadResult::failure(
      ResultCode::RuntimeRetryExhausted,
      std::string("plugin.echo"),
      PluginOperationPhase::Load,
      std::string("plugin load failed"),
      std::string("plugin.load"),
      std::string("PluginManagerBoundaryContractTest"),
      std::string("observation:plugin.echo.load.fail"));
  assert_true(load_failure.references_only_contract_error_types(),
              "plugin load failures should stay inside contracts ResultCode/ErrorInfo types");

  const auto unload_failure = PluginUnloadResult::failure(
      ResultCode::RuntimeRetryExhausted,
      std::string("plugin.echo"),
      std::string("plugin unload failed"),
      std::string("plugin.unload"),
      std::string("PluginManagerBoundaryContractTest"),
      std::string("observation:plugin.echo.unload.fail"));
  assert_true(unload_failure.references_only_contract_error_types(),
              "plugin unload failures should stay inside contracts ResultCode/ErrorInfo types");
}

}  // namespace

int main() {
  try {
    test_plugin_manager_results_keep_contract_error_types_and_optional_report_objects();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}