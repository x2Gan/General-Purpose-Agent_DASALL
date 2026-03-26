#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "error/ErrorInfo.h"
#include "error/ResultCode.h"

namespace dasall::infra {

struct InfrastructureConfig {
  std::string profile = "default";
};

struct InfraCommandRequest {
  std::string name;

  [[nodiscard]] bool is_valid() const {
    return !name.empty();
  }
};

struct InfraOperationResult {
  bool ok = false;
  contracts::ResultCode result_code = contracts::ResultCode::RuntimeRetryExhausted;
  std::optional<contracts::ErrorInfo> error;

  [[nodiscard]] static InfraOperationResult success() {
    return InfraOperationResult{
        .ok = true,
        .result_code = contracts::ResultCode::RuntimeRetryExhausted,
        .error = std::nullopt,
    };
  }

  [[nodiscard]] static InfraOperationResult failure(
      contracts::ResultCode result_code,
      std::string message,
      std::string stage,
      std::string source_ref) {
    return InfraOperationResult{
        .ok = false,
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
                .ref_type = "infra",
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

class IInfrastructureService {
 public:
  virtual ~IInfrastructureService() = default;

  virtual InfraOperationResult init(const InfrastructureConfig& config) = 0;
  virtual InfraOperationResult start() = 0;
  virtual InfraOperationResult stop(std::uint32_t timeout_ms) = 0;
  virtual InfraOperationResult execute(const InfraCommandRequest& command) = 0;
};

class InfraServiceFacade final : public IInfrastructureService {
 public:
  InfraServiceFacade() = default;

  InfraOperationResult init(const InfrastructureConfig& config) override;
  InfraOperationResult start() override;
  InfraOperationResult stop(std::uint32_t timeout_ms) override;
  InfraOperationResult execute(const InfraCommandRequest& command) override;

  [[nodiscard]] std::string_view lifecycle_state_name() const;

 private:
  enum class LifecycleState {
    Created,
    Initialized,
    Started,
    Stopped,
  };

  [[nodiscard]] InfraOperationResult invalid_transition(
      std::string_view operation,
      std::string_view expected_state) const;

  LifecycleState lifecycle_state_ = LifecycleState::Created;
  InfrastructureConfig last_config_{};
};

}  // namespace dasall::infra