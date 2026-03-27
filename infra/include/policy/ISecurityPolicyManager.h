#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "error/ErrorInfo.h"
#include "error/ResultCode.h"

#include "PolicyBundle.h"
#include "PolicyDecisionRef.h"
#include "PolicyPatch.h"
#include "PolicySnapshot.h"

namespace dasall::infra::policy {

struct ValidationReport {
  std::vector<std::string> blocking_errors;
  std::vector<std::string> warnings;
  std::vector<std::string> invalid_rule_ids;
  std::vector<std::string> field_paths;

  [[nodiscard]] bool has_blocking_errors() const {
    return !blocking_errors.empty();
  }
};

struct PolicyOpResult {
  bool ok = false;
  bool rolled_back = false;
  bool dry_run = false;
  std::string snapshot_id;
  std::uint64_t generation = 0;
  contracts::ResultCode result_code = contracts::ResultCode::RuntimeRetryExhausted;
  std::optional<contracts::ErrorInfo> error;

  [[nodiscard]] static PolicyOpResult success(std::string snapshot_id,
                                              std::uint64_t generation,
                                              bool rolled_back = false,
                                              bool dry_run = false) {
    return PolicyOpResult{
        .ok = true,
        .rolled_back = rolled_back,
        .dry_run = dry_run,
        .snapshot_id = std::move(snapshot_id),
        .generation = generation,
        .result_code = contracts::ResultCode::RuntimeRetryExhausted,
        .error = std::nullopt,
    };
  }

  [[nodiscard]] static PolicyOpResult failure(contracts::ResultCode result_code,
                                              std::string message,
                                              std::string stage,
                                              std::string source_ref) {
    return PolicyOpResult{
        .ok = false,
        .rolled_back = false,
        .dry_run = false,
        .snapshot_id = {},
        .generation = 0,
        .result_code = result_code,
        .error = contracts::ErrorInfo{
            .failure_type = contracts::classify_result_code(result_code),
            .retryable = false,
            .safe_to_replan = false,
            .details = contracts::ErrorDetails{
                .code = static_cast<int>(result_code),
                .message = std::move(message),
                .stage = std::move(stage),
            },
            .source_ref = contracts::ErrorSourceRefMinimal{
                .ref_type = "infra.policy",
                .ref_id = std::move(source_ref),
            },
        },
    };
  }

  [[nodiscard]] bool references_only_contract_error_types() const {
    if (!error.has_value()) {
      return ok;
    }

    return error->failure_type.has_value() &&
           *error->failure_type == contracts::classify_result_code(result_code);
  }
};

class ISecurityPolicyManager {
 public:
  virtual ~ISecurityPolicyManager() = default;

  virtual PolicyOpResult load_policy(const PolicyBundle& bundle) = 0;
  virtual PolicyOpResult apply_patch(const PolicyPatch& patch) = 0;
  virtual ValidationReport dry_run_patch(const PolicyPatch& patch) = 0;
  [[nodiscard]] virtual PolicySnapshot snapshot() const = 0;
  virtual PolicyOpResult rollback(const std::string& snapshot_id) = 0;
  [[nodiscard]] virtual PolicyDecisionRef evaluate(const PolicyQueryContext& query) const = 0;
};

}  // namespace dasall::infra::policy