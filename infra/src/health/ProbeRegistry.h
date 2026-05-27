#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "health/IHealthMonitor.h"
#include "health/ProbeTypes.h"

namespace dasall::infra {

struct ProbeRegistryRegisterResult {
  bool ok = false;
  std::optional<contracts::ResultCode> result_code;
  std::optional<contracts::ErrorInfo> error;
  ProbeDescriptor descriptor;

  [[nodiscard]] static ProbeRegistryRegisterResult success(ProbeDescriptor descriptor) {
    return ProbeRegistryRegisterResult{
        .ok = true,
        .result_code = std::nullopt,
        .error = std::nullopt,
        .descriptor = std::move(descriptor),
    };
  }

  [[nodiscard]] static ProbeRegistryRegisterResult failure(
      contracts::ResultCode result_code,
      std::string message,
      std::string stage,
      std::string source_ref) {
    return ProbeRegistryRegisterResult{
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
                .ref_type = "infra.health",
                .ref_id = std::move(source_ref),
            },
        },
        .descriptor = {},
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

struct ProbeRegistryRemoveResult {
  bool ok = false;
  std::optional<contracts::ResultCode> result_code;
  std::optional<contracts::ErrorInfo> error;
  ProbeDescriptor descriptor;
  bool removed = false;

  [[nodiscard]] static ProbeRegistryRemoveResult success(ProbeDescriptor descriptor) {
    return ProbeRegistryRemoveResult{
        .ok = true,
        .result_code = std::nullopt,
        .error = std::nullopt,
        .descriptor = std::move(descriptor),
        .removed = true,
    };
  }

  [[nodiscard]] static ProbeRegistryRemoveResult failure(
      contracts::ResultCode result_code,
      std::string message,
      std::string stage,
      std::string source_ref) {
    return ProbeRegistryRemoveResult{
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
                .ref_type = "infra.health",
                .ref_id = std::move(source_ref),
            },
        },
        .descriptor = {},
        .removed = false,
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

class ProbeRegistry {
 public:
  ProbeRegistry() = default;

  [[nodiscard]] ProbeRegistryRegisterResult register_probe(
      const HealthProbeRegistration& registration);
  [[nodiscard]] ProbeRegistryRemoveResult unregister_probe(std::string_view probe_name);
  [[nodiscard]] std::vector<ProbeDescriptor> list_by_group(std::string_view group) const;
  [[nodiscard]] std::optional<ProbeDescriptor> find_descriptor(
      std::string_view probe_name) const;
  [[nodiscard]] IHealthProbe* find_probe(std::string_view probe_name) const;
  [[nodiscard]] std::size_t size() const;

 private:
  struct ProbeEntry {
    ProbeDescriptor descriptor;
    IHealthProbe* probe = nullptr;
    std::shared_ptr<IHealthProbe> keepalive;
  };

  using ProbeMap = std::map<std::string, ProbeEntry>;

  ProbeMap entries_;
};

}  // namespace dasall::infra