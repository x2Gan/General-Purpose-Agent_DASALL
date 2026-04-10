#include "SecretLeaseRegistry.h"

#include <algorithm>
#include <chrono>
#include <utility>

#include "secret/SecretErrors.h"

namespace dasall::infra::secret {
namespace {

constexpr std::string_view kSecretLeaseRegistrySourceRef = "SecretLeaseRegistry";

[[nodiscard]] std::int64_t current_time_unix_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

[[nodiscard]] SecretLeaseState normalize_target_state(SecretLeaseState target_state) {
  switch (target_state) {
    case SecretLeaseState::Released:
    case SecretLeaseState::Expired:
    case SecretLeaseState::Revoked:
    case SecretLeaseState::Stale:
      return target_state;
    case SecretLeaseState::Unspecified:
    case SecretLeaseState::Active:
      break;
  }

  return SecretLeaseState::Expired;
}

[[nodiscard]] std::string resolve_secret_name(const SecretHandle& handle,
                                              const SecretLease& lease) {
  if (!handle.secret_name.empty()) {
    return handle.secret_name;
  }

  const auto scheme_separator = lease.handle_id.find("://");
  if (scheme_separator == std::string::npos) {
    return lease.handle_id.empty() ? std::string("unresolved-secret") : lease.handle_id;
  }

  const auto secret_begin = scheme_separator + 3U;
  const auto version_separator = lease.handle_id.rfind('/');
  if (version_separator == std::string::npos || version_separator <= secret_begin) {
    return lease.handle_id.substr(secret_begin);
  }

  return lease.handle_id.substr(secret_begin, version_separator - secret_begin);
}

[[nodiscard]] SecretLeaseCreateResult make_create_failure(SecretErrorCode error_code,
                                                          std::string message,
                                                          std::string stage) {
  const SecretErrorMapping mapping = map_secret_error_code(error_code);
  return SecretLeaseCreateResult::failure(
      mapping.result_code,
      std::string(secret_error_code_name(error_code)) + ": " + std::move(message),
      std::move(stage),
      std::string(kSecretLeaseRegistrySourceRef));
}

[[nodiscard]] SecretLifecycleResult make_lifecycle_failure(std::string secret_name,
                                                           std::optional<std::string> lease_id,
                                                           SecretErrorCode error_code,
                                                           std::string message,
                                                           std::string stage) {
  const SecretErrorMapping mapping = map_secret_error_code(error_code);
  return SecretLifecycleResult::failure(
      secret_name.empty() ? std::string("unresolved-secret") : std::move(secret_name),
      mapping.result_code,
      std::string(secret_error_code_name(error_code)) + ": " + std::move(message),
      std::move(stage),
      std::string(kSecretLeaseRegistrySourceRef),
      std::move(lease_id));
}

[[nodiscard]] std::int64_t resolve_deadline_ms(const SecretHandle& handle,
                                               std::optional<std::int64_t> deadline_ms) {
  return std::min(handle.expires_at_ms, deadline_ms.value_or(handle.expires_at_ms));
}

}  // namespace

SecretLeaseRegistry::SecretLeaseRegistry(SecretLeaseRegistryOptions options)
    : options_(std::move(options)) {}

SecretLeaseCreateResult SecretLeaseRegistry::create_lease(
    const SecretHandle& handle,
    std::string_view consumer_ref,
    std::uint64_t rotation_epoch,
    std::optional<std::int64_t> deadline_ms) {
  if (!handle.is_valid() || consumer_ref.empty()) {
    return SecretLeaseCreateResult::failure(
        contracts::ResultCode::ValidationFieldMissing,
        "secret lease creation requires a valid handle and non-empty consumer_ref",
        "secret.lease.create",
        std::string(kSecretLeaseRegistrySourceRef));
  }

  const std::int64_t effective_deadline_ms = resolve_deadline_ms(handle, deadline_ms);
  if (effective_deadline_ms < current_time_unix_ms()) {
    return make_create_failure(SecretErrorCode::LeaseExpired,
                               "secret lease deadline already expired during create_lease",
                               "secret.lease.create");
  }

  const std::uint64_t effective_rotation_epoch =
      std::max(options_.initial_rotation_epoch,
               rotation_epoch == 0 ? options_.initial_rotation_epoch : rotation_epoch);
  SecretLease lease{
      .lease_id = options_.lease_prefix + handle.secret_name + "/" + std::to_string(next_lease_sequence_++),
      .handle_id = handle.handle_id,
      .consumer_ref = std::string(consumer_ref),
      .expires_at_ms = effective_deadline_ms,
      .rotation_epoch = effective_rotation_epoch,
      .state = SecretLeaseState::Active,
  };
  leases_[lease.lease_id] = LeaseEntry{
      .handle = handle,
      .lease = lease,
  };
  return SecretLeaseCreateResult::success(std::move(lease));
}

SecretLifecycleResult SecretLeaseRegistry::validate_lease(
    const SecretLease& lease,
    const SecretHandle& handle,
    std::uint64_t current_rotation_epoch) {
  if (!lease.is_valid() || !handle.is_valid()) {
    return SecretLifecycleResult::failure(resolve_secret_name(handle, lease),
                                          contracts::ResultCode::ValidationFieldMissing,
                                          "secret lease validation requires a valid lease and handle",
                                          "secret.lease.validate",
                                          std::string(kSecretLeaseRegistrySourceRef),
                                          lease.lease_id.empty() ? std::nullopt
                                                                 : std::optional<std::string>(lease.lease_id));
  }

  auto lease_it = find_entry(lease.lease_id);
  if (lease_it == leases_.end()) {
    return make_lifecycle_failure(resolve_secret_name(handle, lease),
                                  lease.lease_id,
                                  SecretErrorCode::LeaseExpired,
                                  "secret lease is not registered",
                                  "secret.lease.validate");
  }

  if (lease_it->second.handle.handle_id != handle.handle_id ||
      lease_it->second.handle.version != handle.version ||
      lease_it->second.lease.handle_id != lease.handle_id ||
      lease_it->second.lease.rotation_epoch != lease.rotation_epoch ||
      (current_rotation_epoch > 0 && lease_it->second.lease.rotation_epoch != current_rotation_epoch)) {
    lease_it->second.lease.state = SecretLeaseState::Stale;
    return make_lifecycle_failure(lease_it->second.handle.secret_name,
                                  lease_it->second.lease.lease_id,
                                  SecretErrorCode::VersionStale,
                                  "secret lease no longer matches the current handle or rotation epoch",
                                  "secret.lease.validate");
  }

  if (lease_it->second.lease.expires_at_ms < current_time_unix_ms() ||
      handle.expires_at_ms < current_time_unix_ms()) {
    lease_it->second.lease.state = SecretLeaseState::Expired;
    return make_lifecycle_failure(lease_it->second.handle.secret_name,
                                  lease_it->second.lease.lease_id,
                                  SecretErrorCode::LeaseExpired,
                                  "secret lease expired before validation completed",
                                  "secret.lease.validate");
  }

  if (lease_it->second.lease.state == SecretLeaseState::Stale) {
    return make_lifecycle_failure(lease_it->second.handle.secret_name,
                                  lease_it->second.lease.lease_id,
                                  SecretErrorCode::VersionStale,
                                  "secret lease is stale and must be reacquired",
                                  "secret.lease.validate");
  }

  if (lease_it->second.lease.state != SecretLeaseState::Active) {
    return make_lifecycle_failure(lease_it->second.handle.secret_name,
                                  lease_it->second.lease.lease_id,
                                  SecretErrorCode::LeaseExpired,
                                  "secret lease is no longer active",
                                  "secret.lease.validate");
  }

  return SecretLifecycleResult::success(lease_it->second.handle.secret_name,
                                        lease_it->second.lease.lease_id);
}

SecretLifecycleResult SecretLeaseRegistry::expire_lease(const SecretLease& lease,
                                                        SecretLeaseState target_state) {
  if (!lease.is_valid()) {
    return SecretLifecycleResult::failure(resolve_secret_name({}, lease),
                                          contracts::ResultCode::ValidationFieldMissing,
                                          "secret lease expiration requires a valid lease",
                                          "secret.lease.expire",
                                          std::string(kSecretLeaseRegistrySourceRef),
                                          lease.lease_id.empty() ? std::nullopt
                                                                 : std::optional<std::string>(lease.lease_id));
  }

  auto lease_it = find_entry(lease.lease_id);
  if (lease_it == leases_.end()) {
    return make_lifecycle_failure(resolve_secret_name({}, lease),
                                  lease.lease_id,
                                  SecretErrorCode::LeaseExpired,
                                  "secret lease is not registered",
                                  "secret.lease.expire");
  }

  lease_it->second.lease.state = normalize_target_state(target_state);
  return SecretLifecycleResult::success(lease_it->second.handle.secret_name,
                                        lease_it->second.lease.lease_id);
}

SecretLifecycleResult SecretLeaseRegistry::release_lease(const SecretLease& lease) {
  if (!lease.is_valid()) {
    return SecretLifecycleResult::failure(resolve_secret_name({}, lease),
                                          contracts::ResultCode::ValidationFieldMissing,
                                          "secret lease release requires a valid lease",
                                          "secret.lease.release",
                                          std::string(kSecretLeaseRegistrySourceRef),
                                          lease.lease_id.empty() ? std::nullopt
                                                                 : std::optional<std::string>(lease.lease_id));
  }

  auto lease_it = find_entry(lease.lease_id);
  if (lease_it == leases_.end()) {
    return make_lifecycle_failure(resolve_secret_name({}, lease),
                                  lease.lease_id,
                                  SecretErrorCode::LeaseExpired,
                                  "secret lease is not registered",
                                  "secret.lease.release");
  }

  if (lease_it->second.lease.expires_at_ms < current_time_unix_ms()) {
    lease_it->second.lease.state = SecretLeaseState::Expired;
    return make_lifecycle_failure(lease_it->second.handle.secret_name,
                                  lease_it->second.lease.lease_id,
                                  SecretErrorCode::LeaseExpired,
                                  "secret lease expired before release completed",
                                  "secret.lease.release");
  }

  if (lease_it->second.lease.state == SecretLeaseState::Stale) {
    return make_lifecycle_failure(lease_it->second.handle.secret_name,
                                  lease_it->second.lease.lease_id,
                                  SecretErrorCode::VersionStale,
                                  "secret lease is stale and cannot be released as active",
                                  "secret.lease.release");
  }

  if (lease_it->second.lease.state != SecretLeaseState::Active) {
    return make_lifecycle_failure(lease_it->second.handle.secret_name,
                                  lease_it->second.lease.lease_id,
                                  SecretErrorCode::LeaseExpired,
                                  "secret lease is no longer active",
                                  "secret.lease.release");
  }

  lease_it->second.lease.state = SecretLeaseState::Released;
  return SecretLifecycleResult::success(lease_it->second.handle.secret_name,
                                        lease_it->second.lease.lease_id);
}

void SecretLeaseRegistry::expire_secret_leases(const std::string_view& secret_name,
                                               SecretLeaseState target_state) {
  const SecretLeaseState normalized_state = normalize_target_state(target_state);
  for (auto& lease_entry : leases_) {
    auto& entry = lease_entry.second;
    if (entry.handle.secret_name == secret_name && entry.lease.state == SecretLeaseState::Active) {
      entry.lease.state = normalized_state;
    }
  }
}

std::size_t SecretLeaseRegistry::active_lease_count() const {
  return static_cast<std::size_t>(std::count_if(
      leases_.begin(),
      leases_.end(),
      [](const auto& lease_entry) {
        return lease_entry.second.lease.state == SecretLeaseState::Active;
      }));
}

SecretLeaseRegistry::LeaseMap::iterator SecretLeaseRegistry::find_entry(
    std::string_view lease_id) {
  return leases_.find(std::string(lease_id));
}

SecretLeaseRegistry::LeaseMap::const_iterator SecretLeaseRegistry::find_entry(
    std::string_view lease_id) const {
  return leases_.find(std::string(lease_id));
}

}  // namespace dasall::infra::secret