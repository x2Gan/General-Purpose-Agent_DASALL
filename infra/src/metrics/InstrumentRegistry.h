#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "error/ErrorInfo.h"
#include "error/ResultCode.h"
#include "metrics/IMeter.h"
#include "metrics/MetricTypes.h"

namespace dasall::infra::metrics {

struct InstrumentRegistrationResult {
  bool ok = false;
  bool created = false;
  std::optional<contracts::ResultCode> result_code;
  std::optional<contracts::ErrorInfo> error;
  InstrumentHandle handle;

  [[nodiscard]] static InstrumentRegistrationResult success(InstrumentHandle handle,
                                                            bool created) {
    return InstrumentRegistrationResult{
        .ok = true,
        .created = created,
        .result_code = std::nullopt,
        .error = std::nullopt,
        .handle = std::move(handle),
    };
  }

  [[nodiscard]] static InstrumentRegistrationResult failure(
      contracts::ResultCode result_code,
      std::string message,
      std::string stage,
      std::string source_ref) {
    return InstrumentRegistrationResult{
        .ok = false,
        .created = false,
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
                .ref_type = "infra.metrics",
                .ref_id = std::move(source_ref),
            },
        },
        .handle = {},
    };
  }

  [[nodiscard]] bool references_only_contract_error_types() const {
    if (!result_code.has_value() && !error.has_value()) {
      return ok && handle.is_valid();
    }

    return result_code.has_value() && error.has_value() &&
           error->failure_type.has_value() &&
           *error->failure_type == contracts::classify_result_code(*result_code);
  }
};

class InstrumentRegistry {
 public:
  InstrumentRegistry() = default;

  [[nodiscard]] InstrumentRegistrationResult register_identity(
      const MetricIdentity& identity);
  [[nodiscard]] std::optional<InstrumentHandle> find_identity(
      std::string_view metric_name) const;
  [[nodiscard]] std::size_t size() const;

 private:
  struct InstrumentEntry {
    MetricIdentity identity;
    InstrumentHandle handle;
  };

  using InstrumentMap = std::map<std::string, InstrumentEntry>;

  InstrumentMap entries_;
};

}  // namespace dasall::infra::metrics