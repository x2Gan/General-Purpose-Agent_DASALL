#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

#include "../../../infra/include/config/IConfigValidator.h"
#include "support/TestAssertions.h"

namespace {

class BoundaryConfigValidator final : public dasall::infra::config::IConfigValidator {
 public:
  [[nodiscard]] dasall::infra::config::ConfigValidationResult validate(
      const dasall::infra::config::ConfigSnapshot& snapshot) const override {
    if (!snapshot.is_valid()) {
      return dasall::infra::config::ConfigValidationResult::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          "config snapshot is required",
          "config.validate",
          "BoundaryConfigValidator",
          dasall::infra::config::ConfigValidationReport{
              .issues = {dasall::infra::config::ValidationIssue{
                  .key_path = std::string("snapshot"),
                  .code = std::string("cfg_invalid_snapshot"),
                  .severity = dasall::infra::config::ValidationSeverity::Error,
                  .message = std::string("config snapshot must remain structurally valid"),
              }},
          });
    }

    return dasall::infra::config::ConfigValidationResult::success();
  }

  [[nodiscard]] dasall::infra::config::ConfigValidationResult validate_patch(
      const dasall::infra::config::ConfigSnapshot& current_snapshot,
      const dasall::infra::config::ConfigPatch& patch) const override {
    if (!current_snapshot.is_valid() || !patch.is_valid()) {
      return dasall::infra::config::ConfigValidationResult::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          "current snapshot and runtime patch must remain explicit",
          "config.validate_patch",
          "BoundaryConfigValidator",
          dasall::infra::config::ConfigValidationReport{
              .issues = {dasall::infra::config::ValidationIssue{
                  .key_path = std::string("patches"),
                  .code = std::string("cfg_invalid_patch"),
                  .severity = dasall::infra::config::ValidationSeverity::Error,
                  .message = std::string("runtime patch metadata or protected paths violate the frozen validator contract"),
              }},
          });
    }

    return dasall::infra::config::ConfigValidationResult::success();
  }
};

void test_config_validator_interface_keeps_report_local_and_error_mapping_contractual() {
  using dasall::contracts::ErrorInfo;
  using dasall::contracts::ResultCode;
  using dasall::infra::config::ConfigValidationReport;
  using dasall::infra::config::ConfigValidationResult;
  using dasall::infra::config::ValidationIssue;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(ConfigValidationReport{}.issues), std::vector<ValidationIssue>>);
  static_assert(std::is_same_v<decltype(ConfigValidationResult{}.result_code), ResultCode>);
  static_assert(std::is_same_v<decltype(ConfigValidationResult{}.error_info), std::optional<ErrorInfo>>);

  const auto failure = ConfigValidationResult::failure(
      ResultCode::ValidationFieldMissing,
      "runtime patch metadata is required",
      "config.validate_patch",
      "IConfigValidator",
      ConfigValidationReport{
          .issues = {ValidationIssue{
              .key_path = std::string("patches"),
              .code = std::string("cfg_invalid_patch"),
              .severity = dasall::infra::config::ValidationSeverity::Error,
              .message = std::string("runtime patch metadata or protected paths violate the frozen validator contract"),
          }},
      });

  assert_true(!failure.accepted,
              "config validator failure should remain explicit when validation rejects a snapshot or patch");
  assert_true(failure.report.is_valid() && failure.report.has_errors(),
              "config validator boundary should preserve locatable validation issues inside the local config report");
  assert_true(failure.references_only_contract_error_types(),
              "config validator boundary should expose only contracts ResultCode/ErrorInfo types across the boundary");
}

void test_config_validator_interface_rejects_invalid_runtime_patch_inputs() {
  using dasall::tests::support::assert_true;

  BoundaryConfigValidator validator;
  const auto result = validator.validate_patch(dasall::infra::config::ConfigSnapshot{},
                                               dasall::infra::config::ConfigPatch{});

  assert_true(!result.accepted,
              "config validator boundary should reject empty snapshot and patch placeholders");
  assert_true(result.references_only_contract_error_types(),
              "config validator boundary failures should remain inside contracts ResultCode/ErrorInfo types");
}

}  // namespace

int main() {
  try {
    test_config_validator_interface_keeps_report_local_and_error_mapping_contractual();
    test_config_validator_interface_rejects_invalid_runtime_patch_inputs();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}