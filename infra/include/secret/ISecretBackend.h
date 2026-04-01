#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "secret/SecretTypes.h"

namespace dasall::infra::secret {

enum class SecretBackendState {
  Unknown = 0,
  Available = 1,
  Degraded = 2,
  Unavailable = 3,
};

struct SecretBackendRecord {
  SecretDescriptor descriptor;
  std::string backend_ref;
  std::string version;
  std::string cipher_ref;
  bool encrypted_at_rest = true;

  [[nodiscard]] bool is_valid() const {
    return descriptor.is_valid() && !backend_ref.empty() && !version.empty() &&
           !cipher_ref.empty();
  }
};

struct SecretVersionPromotionRequest {
  std::string secret_name;
  std::string previous_version;
  std::string candidate_version;
  std::uint64_t rotation_epoch = 0;
  bool validate_only = false;

  [[nodiscard]] bool is_valid() const {
    return !secret_name.empty() && !previous_version.empty() && !candidate_version.empty() &&
           previous_version != candidate_version && rotation_epoch > 0;
  }
};

struct SecretBackendStatus {
  SecretBackendState state = SecretBackendState::Unknown;
  std::string backend_ref;
  bool rate_limited = false;
  bool read_only_fallback_ready = false;
  std::optional<contracts::ResultCode> last_error_code;
  std::string detail_ref;

  [[nodiscard]] bool is_valid() const {
    if (state == SecretBackendState::Unknown || backend_ref.empty()) {
      return false;
    }

    if (last_error_code.has_value()) {
      return !detail_ref.empty() &&
             contracts::classify_result_code(*last_error_code) !=
                 contracts::ResultCodeCategory::Unknown;
    }

    return true;
  }
};

struct SecretBackendFetchResult {
  bool ok = false;
  SecretBackendRecord record;
  std::optional<contracts::ResultCode> result_code;
  std::optional<contracts::ErrorInfo> error_info;

  [[nodiscard]] static SecretBackendFetchResult success(SecretBackendRecord record) {
    return SecretBackendFetchResult{
        .ok = true,
        .record = std::move(record),
        .result_code = std::nullopt,
        .error_info = std::nullopt,
    };
  }

  [[nodiscard]] static SecretBackendFetchResult failure(contracts::ResultCode result_code,
                                                        std::string message,
                                                        std::string stage,
                                                        std::string source_ref) {
    return SecretBackendFetchResult{
        .ok = false,
        .record = {},
        .result_code = result_code,
        .error_info = detail::make_error_info(result_code,
                                              std::move(message),
                                              std::move(stage),
                                              std::move(source_ref)),
    };
  }

  [[nodiscard]] bool references_only_contract_error_types() const {
    if (!error_info.has_value()) {
      return ok && !result_code.has_value();
    }

    return result_code.has_value() && error_info->failure_type.has_value() &&
           *error_info->failure_type == contracts::classify_result_code(*result_code);
  }

  [[nodiscard]] bool is_valid() const {
    if (ok) {
      return record.is_valid() && !result_code.has_value() && !error_info.has_value();
    }

    return result_code.has_value() && error_info.has_value() &&
           references_only_contract_error_types();
  }
};

class ISecretBackend {
 public:
  virtual ~ISecretBackend() = default;

  [[nodiscard]] virtual SecretBackendFetchResult fetch_record(
      const SecretQuery& query) = 0;

  [[nodiscard]] virtual SecretMaterializationResult materialize_record(
      const SecretBackendRecord& record,
      const SecretAccessContext& access_context) = 0;

  [[nodiscard]] virtual RotationResult promote_version(
      const SecretVersionPromotionRequest& request) = 0;

  [[nodiscard]] virtual SecretLifecycleResult revoke_version(
      std::string_view secret_name,
      std::string_view version) = 0;

  [[nodiscard]] virtual SecretBackendStatus get_backend_status() const = 0;
};

}  // namespace dasall::infra::secret