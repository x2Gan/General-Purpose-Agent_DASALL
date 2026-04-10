#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "secret/SecretTypes.h"

namespace dasall::infra::secret {

struct SecretLeaseRegistryOptions {
  std::string lease_prefix = "secret-lease://";
  std::uint64_t initial_rotation_epoch = 1;
};

struct SecretLeaseCreateResult {
  bool ok = false;
  SecretLease lease;
  std::optional<contracts::ResultCode> result_code;
  std::optional<contracts::ErrorInfo> error_info;

  [[nodiscard]] static SecretLeaseCreateResult success(SecretLease lease) {
    return SecretLeaseCreateResult{
        .ok = true,
        .lease = std::move(lease),
        .result_code = std::nullopt,
        .error_info = std::nullopt,
    };
  }

  [[nodiscard]] static SecretLeaseCreateResult failure(contracts::ResultCode result_code,
                                                       std::string message,
                                                       std::string stage,
                                                       std::string source_ref) {
    return SecretLeaseCreateResult{
        .ok = false,
        .lease = {},
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
      return lease.is_valid() && !result_code.has_value() && !error_info.has_value();
    }

    return result_code.has_value() && error_info.has_value() &&
           references_only_contract_error_types();
  }
};

class SecretLeaseRegistry {
 public:
  explicit SecretLeaseRegistry(SecretLeaseRegistryOptions options = {});

  [[nodiscard]] SecretLeaseCreateResult create_lease(
      const SecretHandle& handle,
      std::string_view consumer_ref,
      std::uint64_t rotation_epoch = 0,
      std::optional<std::int64_t> deadline_ms = std::nullopt);

  [[nodiscard]] SecretLifecycleResult validate_lease(
      const SecretLease& lease,
      const SecretHandle& handle,
      std::uint64_t current_rotation_epoch);

  [[nodiscard]] SecretLifecycleResult expire_lease(
      const SecretLease& lease,
      SecretLeaseState target_state = SecretLeaseState::Expired);

  [[nodiscard]] SecretLifecycleResult release_lease(
      const SecretLease& lease);

  void expire_secret_leases(
      const std::string_view& secret_name,
      SecretLeaseState target_state = SecretLeaseState::Revoked);

  [[nodiscard]] std::size_t active_lease_count() const;

 private:
  struct LeaseEntry {
    SecretHandle handle;
    SecretLease lease;
  };

  using LeaseMap = std::map<std::string, LeaseEntry>;

  [[nodiscard]] LeaseMap::iterator find_entry(std::string_view lease_id);
  [[nodiscard]] LeaseMap::const_iterator find_entry(std::string_view lease_id) const;

  SecretLeaseRegistryOptions options_;
  LeaseMap leases_;
  std::uint64_t next_lease_sequence_ = 1;
};

}  // namespace dasall::infra::secret