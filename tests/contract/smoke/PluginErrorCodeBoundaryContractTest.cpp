#include <array>
#include <exception>
#include <iostream>
#include <string>
#include <type_traits>

#include "plugin/PluginErrorCode.h"
#include "support/TestAssertions.h"

namespace {

void test_plugin_error_codes_map_only_to_existing_contract_result_codes() {
  using dasall::contracts::ResultCode;
  using dasall::infra::plugin::map_plugin_error_code;
  using dasall::infra::plugin::PluginErrorCode;
  using dasall::infra::plugin::PluginErrorMapping;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(PluginErrorMapping{}.result_code), ResultCode>);

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
    assert_true(mapping.result_code == ResultCode::ValidationFieldMissing ||
                    mapping.result_code == ResultCode::PolicyDenied ||
                    mapping.result_code == ResultCode::RuntimeRetryExhausted,
                "plugin private errors should map only to existing contracts result codes");
  }
}

void test_plugin_error_code_names_stay_private_to_plugin_boundary() {
  using dasall::infra::plugin::plugin_error_code_name;
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
    const auto name = plugin_error_code_name(code);
    assert_true(name.starts_with("INF_E_PLUGIN_"),
                "plugin private error names should remain inside INF_E_PLUGIN_* local namespace");
  }
}

}  // namespace

int main() {
  try {
    test_plugin_error_codes_map_only_to_existing_contract_result_codes();
    test_plugin_error_code_names_stay_private_to_plugin_boundary();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}