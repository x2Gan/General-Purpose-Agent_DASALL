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

void test_plugin_manager_results_keep_contract_error_types_and_ref_only_report_links() {
  using dasall::contracts::ErrorInfo;
  using dasall::contracts::ResultCode;
  using dasall::infra::plugin::ActivePluginSet;
  using dasall::infra::plugin::PluginDescriptor;
  using dasall::infra::plugin::PluginLoadResult;
  using dasall::infra::plugin::PluginUnloadResult;
  using dasall::infra::plugin::PluginValidationResult;
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
  static_assert(
      std::is_same_v<decltype(ActivePluginSet{}.active_plugins), std::vector<PluginDescriptor>>);
  static_assert(!HasSignatureReport<PluginValidationResult>::value);
  static_assert(!HasCompatibilityReport<PluginValidationResult>::value);

  const auto validation_failure = PluginValidationResult::failure(
      ResultCode::ValidationFieldMissing,
      std::string("plugin.echo"),
      std::string("plugin validation failed"),
      std::string("plugin.validate"),
      std::string("PluginManagerBoundaryContractTest"),
      std::string("observation:plugin.echo.validate.fail"));
  assert_true(validation_failure.references_only_contract_error_types(),
              "plugin validation failures should stay inside contracts ResultCode/ErrorInfo types");

  const auto load_failure = PluginLoadResult::failure(
      ResultCode::RuntimeRetryExhausted,
      std::string("plugin.echo"),
      dasall::infra::plugin::PluginOperationPhase::Load,
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
    test_plugin_manager_results_keep_contract_error_types_and_ref_only_report_links();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}