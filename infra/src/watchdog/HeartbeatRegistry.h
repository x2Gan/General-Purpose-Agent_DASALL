#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "error/ErrorInfo.h"
#include "error/ResultCode.h"
#include "watchdog/WatchedEntityDescriptor.h"
#include "watchdog/WatchdogErrors.h"

namespace dasall::infra::watchdog {

struct HeartbeatRegistryRegisterResult {
  bool ok = false;
  std::optional<WatchdogErrorCode> watchdog_code;
  std::optional<contracts::ResultCode> result_code;
  std::optional<contracts::ErrorInfo> error;
  WatchedEntityDescriptor descriptor{};
  std::size_t total_entities = 0;

  [[nodiscard]] static HeartbeatRegistryRegisterResult success(
      WatchedEntityDescriptor descriptor,
      std::size_t total_entities) {
    return HeartbeatRegistryRegisterResult{
        .ok = true,
        .watchdog_code = std::nullopt,
        .result_code = std::nullopt,
        .error = std::nullopt,
        .descriptor = std::move(descriptor),
        .total_entities = total_entities,
    };
  }

  [[nodiscard]] static HeartbeatRegistryRegisterResult failure(
      std::optional<WatchdogErrorCode> watchdog_code,
      contracts::ResultCode result_code,
      std::string message,
      std::string stage,
      std::string source_ref) {
    return HeartbeatRegistryRegisterResult{
        .ok = false,
        .watchdog_code = watchdog_code,
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
                .ref_type = "infra.watchdog",
                .ref_id = std::move(source_ref),
            },
        },
        .descriptor = {},
        .total_entities = 0,
    };
  }

  [[nodiscard]] bool references_only_contract_error_types() const {
    if (!result_code.has_value() && !error.has_value()) {
      return ok;
    }

    return result_code.has_value() && error.has_value() &&
           error->failure_type.has_value() &&
           *error->failure_type == contracts::classify_result_code(*result_code);
  }
};

struct HeartbeatRegistryRemoveResult {
  bool ok = false;
  std::optional<WatchdogErrorCode> watchdog_code;
  std::optional<contracts::ResultCode> result_code;
  std::optional<contracts::ErrorInfo> error;
  WatchedEntityDescriptor descriptor{};
  bool removed = false;
  std::size_t total_entities = 0;

  [[nodiscard]] static HeartbeatRegistryRemoveResult success(
      WatchedEntityDescriptor descriptor,
      std::size_t total_entities) {
    return HeartbeatRegistryRemoveResult{
        .ok = true,
        .watchdog_code = std::nullopt,
        .result_code = std::nullopt,
        .error = std::nullopt,
        .descriptor = std::move(descriptor),
        .removed = true,
        .total_entities = total_entities,
    };
  }

  [[nodiscard]] static HeartbeatRegistryRemoveResult failure(
      std::optional<WatchdogErrorCode> watchdog_code,
      contracts::ResultCode result_code,
      std::string message,
      std::string stage,
      std::string source_ref) {
    return HeartbeatRegistryRemoveResult{
        .ok = false,
        .watchdog_code = watchdog_code,
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
                .ref_type = "infra.watchdog",
                .ref_id = std::move(source_ref),
            },
        },
        .descriptor = {},
        .removed = false,
        .total_entities = 0,
    };
  }

  [[nodiscard]] bool references_only_contract_error_types() const {
    if (!result_code.has_value() && !error.has_value()) {
      return ok && removed;
    }

    return result_code.has_value() && error.has_value() &&
           error->failure_type.has_value() &&
           *error->failure_type == contracts::classify_result_code(*result_code);
  }
};

struct HeartbeatRegistryQueryResult {
  bool ok = false;
  std::optional<WatchdogErrorCode> watchdog_code;
  std::optional<contracts::ResultCode> result_code;
  std::optional<contracts::ErrorInfo> error;
  WatchedEntityDescriptor descriptor{};
  bool found = false;

  [[nodiscard]] static HeartbeatRegistryQueryResult success(
      WatchedEntityDescriptor descriptor) {
    return HeartbeatRegistryQueryResult{
        .ok = true,
        .watchdog_code = std::nullopt,
        .result_code = std::nullopt,
        .error = std::nullopt,
        .descriptor = std::move(descriptor),
        .found = true,
    };
  }

  [[nodiscard]] static HeartbeatRegistryQueryResult failure(
      std::optional<WatchdogErrorCode> watchdog_code,
      contracts::ResultCode result_code,
      std::string message,
      std::string stage,
      std::string source_ref) {
    return HeartbeatRegistryQueryResult{
        .ok = false,
        .watchdog_code = watchdog_code,
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
                .ref_type = "infra.watchdog",
                .ref_id = std::move(source_ref),
            },
        },
        .descriptor = {},
        .found = false,
    };
  }

  [[nodiscard]] bool references_only_contract_error_types() const {
    if (!result_code.has_value() && !error.has_value()) {
      return ok && found;
    }

    return result_code.has_value() && error.has_value() &&
           error->failure_type.has_value() &&
           *error->failure_type == contracts::classify_result_code(*result_code);
  }
};

class HeartbeatRegistry {
 public:
  explicit HeartbeatRegistry(std::size_t max_entities = 1024U)
      : max_entities_(max_entities) {}

  [[nodiscard]] HeartbeatRegistryRegisterResult register_entity(
      const WatchedEntityDescriptor& descriptor);
  [[nodiscard]] HeartbeatRegistryRemoveResult unregister_entity(
      std::string_view entity_id);
  [[nodiscard]] HeartbeatRegistryQueryResult query_entity(
      std::string_view entity_id) const;
  [[nodiscard]] std::vector<WatchedEntityDescriptor> list_entities() const;
  [[nodiscard]] std::size_t size() const;
  [[nodiscard]] std::size_t max_entities() const;

 private:
  using EntityMap = std::map<std::string, WatchedEntityDescriptor>;

  EntityMap entries_;
  std::size_t max_entities_ = 1024U;
};

}  // namespace dasall::infra::watchdog