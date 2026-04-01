#include <array>
#include <exception>
#include <iostream>
#include <string>

#include "plugin/PluginErrorCode.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

void test_plugin_error_code_names_and_mapping_are_stable() {
  using dasall::infra::plugin::PluginErrorCode;
  using dasall::infra::plugin::map_plugin_error_code;
  using dasall::infra::plugin::plugin_error_code_name;
  using dasall::tests::support::assert_equal;

  assert_equal(std::string("INF_E_PLUGIN_VALIDATE_FAIL"),
               std::string(plugin_error_code_name(PluginErrorCode::ValidateFail)),
               "plugin validate fail code name should remain stable");
  assert_equal(std::string("INF_E_PLUGIN_POLICY_DENIED"),
               std::string(plugin_error_code_name(PluginErrorCode::PolicyDenied)),
               "plugin policy denied code name should remain stable");
  assert_equal(std::string("INF_E_PLUGIN_SIGNATURE_FAIL"),
               std::string(plugin_error_code_name(PluginErrorCode::SignatureFail)),
               "plugin signature fail code name should remain stable");
  assert_equal(std::string("INF_E_PLUGIN_COMPATIBILITY_FAIL"),
               std::string(plugin_error_code_name(PluginErrorCode::CompatibilityFail)),
               "plugin compatibility fail code name should remain stable");
  assert_equal(std::string("INF_E_PLUGIN_LOAD_FAIL"),
               std::string(plugin_error_code_name(PluginErrorCode::LoadFail)),
               "plugin load fail code name should remain stable");
  assert_equal(std::string("INF_E_PLUGIN_UNLOAD_FAIL"),
               std::string(plugin_error_code_name(PluginErrorCode::UnloadFail)),
               "plugin unload fail code name should remain stable");

  assert_equal(static_cast<int>(dasall::contracts::ResultCode::ValidationFieldMissing),
               static_cast<int>(map_plugin_error_code(PluginErrorCode::ValidateFail).result_code),
               "plugin validate fail should map to contracts validation category");
  assert_equal(static_cast<int>(dasall::contracts::ResultCode::PolicyDenied),
               static_cast<int>(map_plugin_error_code(PluginErrorCode::PolicyDenied).result_code),
               "plugin policy denied should map to contracts policy category");
  assert_equal(static_cast<int>(dasall::contracts::ResultCode::RuntimeRetryExhausted),
               static_cast<int>(map_plugin_error_code(PluginErrorCode::LoadFail).result_code),
               "plugin load fail should map to contracts runtime category");
}

void test_plugin_error_code_mapping_covers_all_frozen_codes() {
  using dasall::contracts::classify_result_code;
  using dasall::contracts::ResultCodeCategory;
  using dasall::infra::plugin::map_plugin_error_code;
  using dasall::infra::plugin::PluginErrorCode;
  using dasall::tests::support::assert_true;

  constexpr std::array<PluginErrorCode, 6> kFrozenCodes{
      PluginErrorCode::ValidateFail,
      PluginErrorCode::PolicyDenied,
      PluginErrorCode::SignatureFail,
      PluginErrorCode::CompatibilityFail,
      PluginErrorCode::LoadFail,
      PluginErrorCode::UnloadFail,
  };

  for (const auto code : kFrozenCodes) {
    const auto mapping = map_plugin_error_code(code);
    const auto category = classify_result_code(mapping.result_code);

    assert_true(category == ResultCodeCategory::Validation ||
                    category == ResultCodeCategory::Policy ||
                    category == ResultCodeCategory::Runtime,
                "plugin private error mapping should stay inside existing contracts categories");
    assert_true(!mapping.reason.empty(),
                "each frozen plugin error mapping should carry an observable reason");
  }
}

}  // namespace

int main() {
  try {
    test_plugin_error_code_names_and_mapping_are_stable();
    test_plugin_error_code_mapping_covers_all_frozen_codes();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}