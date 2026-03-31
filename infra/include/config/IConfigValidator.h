#pragma once

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "config/ConfigTypes.h"
#include "error/ErrorInfo.h"
#include "error/ResultCode.h"

namespace dasall::infra::config {

struct ConfigValidationReport {
  std::vector<ValidationIssue> issues;

  [[nodiscard]] bool is_valid() const {
    return std::all_of(issues.begin(), issues.end(), [](const ValidationIssue& issue) {
      return issue.is_valid();
    });
  }

  [[nodiscard]] bool has_errors() const {
    return std::any_of(issues.begin(), issues.end(), [](const ValidationIssue& issue) {
      return issue.severity == ValidationSeverity::Error;
    });
  }
};

struct ConfigValidationResult {
  bool accepted = false;
  ConfigValidationReport report;
  contracts::ResultCode result_code = contracts::ResultCode::RuntimeRetryExhausted;
  std::optional<contracts::ErrorInfo> error_info;

  [[nodiscard]] static ConfigValidationResult success(ConfigValidationReport report = {}) {
    const bool report_is_valid = report.is_valid();
    const bool accepted = report_is_valid && !report.has_errors();

    return ConfigValidationResult{
        .accepted = accepted,
        .report = std::move(report),
        .result_code = contracts::ResultCode::RuntimeRetryExhausted,
        .error_info = std::nullopt,
    };
  }

  [[nodiscard]] static ConfigValidationResult failure(contracts::ResultCode result_code,
                                                      std::string message,
                                                      std::string stage,
                                                      std::string source_ref,
                                                      ConfigValidationReport report = {}) {
    return ConfigValidationResult{
        .accepted = false,
        .report = std::move(report),
        .result_code = result_code,
        .error_info = contracts::ErrorInfo{
            .failure_type = contracts::classify_result_code(result_code),
            .retryable = false,
            .safe_to_replan = false,
            .details = contracts::ErrorDetails{
                .code = static_cast<int>(result_code),
                .message = std::move(message),
                .stage = std::move(stage),
            },
            .source_ref = contracts::ErrorSourceRefMinimal{
                .ref_type = "infra.config",
                .ref_id = std::move(source_ref),
            },
        },
    };
  }

  [[nodiscard]] bool references_only_contract_error_types() const {
    if (!error_info.has_value()) {
      return accepted;
    }

    return error_info->failure_type.has_value() &&
           *error_info->failure_type == contracts::classify_result_code(result_code);
  }
};

class IConfigValidator {
 public:
  virtual ~IConfigValidator() = default;

  [[nodiscard]] virtual ConfigValidationResult validate(const ConfigSnapshot& snapshot) const = 0;
  [[nodiscard]] virtual ConfigValidationResult validate_patch(const ConfigSnapshot& current_snapshot,
                                                              const ConfigPatch& patch) const = 0;
};

}  // namespace dasall::infra::config