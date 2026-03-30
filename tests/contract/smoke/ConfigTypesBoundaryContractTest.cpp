#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include "../../../infra/include/config/ConfigTypes.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

void test_config_types_keep_typed_model_private_to_infra() {
  using dasall::infra::config::ConfigApplyResult;
  using dasall::infra::config::ConfigLayerRef;
  using dasall::infra::config::ConfigPatch;
  using dasall::infra::config::ConfigPatchEntry;
  using dasall::infra::config::ConfigSnapshot;
  using dasall::infra::config::TypedConfig;
  using dasall::infra::config::ValidationIssue;
  using dasall::infra::config::is_runtime_override_protected_path;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(TypedConfig{}.key_path), std::string>);
  static_assert(std::is_same_v<decltype(ConfigPatch{}.patches), std::vector<ConfigPatchEntry>>);
  static_assert(std::is_same_v<decltype(ConfigSnapshot{}.source_chain), std::vector<ConfigLayerRef>>);
  static_assert(std::is_same_v<decltype(ValidationIssue{}.message), std::string>);
  static_assert(std::is_same_v<decltype(ConfigApplyResult{}.rollback_token), std::string>);

  assert_true(is_runtime_override_protected_path("schema_version"),
              "config boundary should keep schema_version protected from runtime overrides");
  assert_true(is_runtime_override_protected_path("enabled_modules.runtime"),
              "config boundary should keep enabled_modules.* inside profile-governed namespaces");
}

void test_config_apply_result_uses_contract_error_types_only() {
  using dasall::contracts::ErrorInfo;
  using dasall::contracts::ResultCode;
  using dasall::infra::config::ConfigApplyResult;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(ConfigApplyResult{}.result_code), ResultCode>);
  static_assert(std::is_same_v<decltype(ConfigApplyResult{}.error_info), std::optional<ErrorInfo>>);

  const auto failure = ConfigApplyResult::failure(ResultCode::ValidationFieldMissing,
                                                  "runtime override patch metadata is required",
                                                  "config.apply_override",
                                                  "IConfigCenter");
  assert_true(!failure.applied,
              "config apply failure should remain explicit when validation rejects a patch");
  assert_true(failure.references_only_contract_error_types(),
              "config apply result should expose only contracts ResultCode/ErrorInfo types across the boundary");
}

}  // namespace

int main() {
  try {
    test_config_types_keep_typed_model_private_to_infra();
    test_config_apply_result_uses_contract_error_types_only();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}