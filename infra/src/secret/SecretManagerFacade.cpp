#include "SecretManagerFacade.h"

#include <algorithm>
#include <chrono>
#include <utility>

#include "secret/SecretErrors.h"

namespace dasall::infra::secret {
namespace {

constexpr std::string_view kSecretManagerFacadeSourceRef = "SecretManagerFacade";

[[nodiscard]] std::int64_t current_time_unix_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

[[nodiscard]] SecretErrorMapping error_mapping(SecretErrorCode error_code) {
  return map_secret_error_code(error_code);
}

[[nodiscard]] SecretHandleResult make_handle_failure(SecretErrorCode error_code,
                                                     std::string message,
                                                     std::string stage) {
  const SecretErrorMapping mapping = error_mapping(error_code);
  return SecretHandleResult::failure(
      mapping.result_code,
      std::string(secret_error_code_name(error_code)) + ": " + std::move(message),
      std::move(stage),
      std::string(kSecretManagerFacadeSourceRef));
}

[[nodiscard]] SecretMaterializationResult make_materialization_failure(SecretErrorCode error_code,
                                                                       std::string message,
                                                                       std::string stage) {
  const SecretErrorMapping mapping = error_mapping(error_code);
  return SecretMaterializationResult::failure(
      mapping.result_code,
      std::string(secret_error_code_name(error_code)) + ": " + std::move(message),
      std::move(stage),
      std::string(kSecretManagerFacadeSourceRef));
}

[[nodiscard]] SecretLifecycleResult make_lifecycle_failure(std::string secret_name,
                                                           SecretErrorCode error_code,
                                                           std::string message,
                                                           std::string stage,
                                                           std::optional<std::string> lease_id = std::nullopt) {
  const SecretErrorMapping mapping = error_mapping(error_code);
  return SecretLifecycleResult::failure(
  secret_name.empty() ? std::string("unresolved-secret") : std::move(secret_name),
      mapping.result_code,
      std::string(secret_error_code_name(error_code)) + ": " + std::move(message),
      std::move(stage),
      std::string(kSecretManagerFacadeSourceRef),
      std::move(lease_id));
}

[[nodiscard]] SecretInspectionResult make_inspection_failure(SecretErrorCode error_code,
                                                             std::string message,
                                                             std::string stage) {
  const SecretErrorMapping mapping = error_mapping(error_code);
  return SecretInspectionResult::failure(
      mapping.result_code,
      std::string(secret_error_code_name(error_code)) + ": " + std::move(message),
      std::move(stage),
      std::string(kSecretManagerFacadeSourceRef));
}

[[nodiscard]] SecretHandle make_handle(const SecretBackendRecord& record,
                                       const SecretManagerFacadeOptions& options) {
  const std::int64_t issued_at_ms = current_time_unix_ms();
  return SecretHandle{
      .handle_id = "secret-handle://" + record.descriptor.secret_name + "/" + record.version,
      .secret_name = record.descriptor.secret_name,
      .version = record.version,
      .backend_ref = record.backend_ref,
      .issued_at_ms = issued_at_ms,
      .expires_at_ms = issued_at_ms + std::max<std::int64_t>(1, options.handle_ttl_ms),
      .redaction_hint = options.redaction_prefix + record.descriptor.secret_name,
  };
}

[[nodiscard]] SecretQuery make_query(std::string_view secret_name,
                                     std::string_view version_hint,
                                     std::string purpose,
                                     SecretAccessMode access_mode) {
  return SecretQuery{
      .secret_name = std::string(secret_name),
      .version_hint = std::string(version_hint),
      .purpose = std::move(purpose),
      .access_mode = access_mode,
  };
}

[[nodiscard]] std::string extract_error_message(const std::optional<contracts::ErrorInfo>& error_info,
                                                std::string fallback_message) {
  if (!error_info.has_value()) {
    return fallback_message;
  }

  if (!error_info->details.message.empty()) {
    return error_info->details.message;
  }

  return fallback_message;
}

[[nodiscard]] std::string resolve_secret_name_from_handle_id(std::string_view handle_id) {
  const auto scheme_separator = handle_id.find("://");
  if (scheme_separator == std::string_view::npos) {
    return handle_id.empty() ? std::string("unresolved-secret") : std::string(handle_id);
  }

  const auto secret_begin = scheme_separator + 3U;
  const auto version_separator = handle_id.rfind('/');
  if (version_separator == std::string_view::npos || version_separator <= secret_begin) {
    return std::string(handle_id.substr(secret_begin));
  }

  return std::string(handle_id.substr(secret_begin, version_separator - secret_begin));
}

}  // namespace

SecretManagerFacade::SecretManagerFacade(std::shared_ptr<ISecretBackend> backend,
                                         SecretManagerFacadeOptions options)
    : backend_(std::move(backend)), options_(std::move(options)) {}

void SecretManagerFacade::set_backend(std::shared_ptr<ISecretBackend> backend) {
  backend_ = std::move(backend);
}

std::size_t SecretManagerFacade::active_lease_count() const {
  return active_leases_.size();
}

bool SecretManagerFacade::has_cached_descriptor(std::string_view secret_name) const {
  return cached_secrets_.find(std::string(secret_name)) != cached_secrets_.end();
}

SecretHandleResult SecretManagerFacade::get_secret(
    const SecretQuery& query,
    const SecretAccessContext& access_context) {
  if (!query.is_valid() || !access_context.is_valid()) {
    return SecretHandleResult::failure(contracts::ResultCode::ValidationFieldMissing,
                                       "secret manager requires a valid query and access context before get_secret",
                                       "secret.get_secret",
                                       std::string(kSecretManagerFacadeSourceRef));
  }

  if (!backend_ready()) {
    return make_handle_failure(SecretErrorCode::BackendUnavailable,
                               "secret backend is unavailable for get_secret",
                               "secret.get_secret");
  }

  const auto fetched = backend_->fetch_record(query);
  if (!fetched.ok) {
    return SecretHandleResult::failure(fetched.result_code.value_or(contracts::ResultCode::ProviderTimeout),
                                       extract_error_message(fetched.error_info,
                                                             "secret backend fetch failed during get_secret"),
                                       "secret.get_secret",
                                       std::string(kSecretManagerFacadeSourceRef));
  }

  cached_secrets_[fetched.record.descriptor.secret_name] = CachedSecretMetadata{
      .descriptor = fetched.record.descriptor,
      .version = fetched.record.version,
      .backend_ref = fetched.record.backend_ref,
  };
  return SecretHandleResult::success(make_handle(fetched.record, options_));
}

SecretMaterializationResult SecretManagerFacade::materialize(
    const SecretHandle& handle,
    const SecretAccessContext& access_context) {
  if (!handle.is_valid() || !access_context.is_valid()) {
    return SecretMaterializationResult::failure(
        contracts::ResultCode::ValidationFieldMissing,
        "secret manager requires a valid handle and access context before materialize",
        "secret.materialize",
        std::string(kSecretManagerFacadeSourceRef));
  }

  if (!backend_ready()) {
    return make_materialization_failure(SecretErrorCode::BackendUnavailable,
                                        "secret backend is unavailable for materialize",
                                        "secret.materialize");
  }

  if (handle.expires_at_ms < current_time_unix_ms()) {
    return make_materialization_failure(SecretErrorCode::LeaseExpired,
                                        "secret handle expired before materialize",
                                        "secret.materialize");
  }

  const auto fetched = backend_->fetch_record(make_query(handle.secret_name,
                                                         handle.version,
                                                         "materialize",
                                                         SecretAccessMode::Materialize));
  if (!fetched.ok) {
    return SecretMaterializationResult::failure(
        fetched.result_code.value_or(contracts::ResultCode::ProviderTimeout),
        extract_error_message(fetched.error_info,
                              "secret backend fetch failed during materialize"),
        "secret.materialize",
        std::string(kSecretManagerFacadeSourceRef));
  }

  const auto materialized = backend_->materialize_record(fetched.record, access_context);
  if (!materialized.ok) {
    return SecretMaterializationResult::failure(
        materialized.result_code.value_or(contracts::ResultCode::ToolExecutionFailed),
        extract_error_message(materialized.error_info,
                              "secret backend materialization failed"),
        "secret.materialize",
        std::string(kSecretManagerFacadeSourceRef));
  }

  active_leases_[materialized.lease.lease_id] = ActiveLeaseEntry{
      .secret_name = fetched.record.descriptor.secret_name,
      .lease = materialized.lease,
  };
  cached_secrets_[fetched.record.descriptor.secret_name] = CachedSecretMetadata{
      .descriptor = fetched.record.descriptor,
      .version = fetched.record.version,
      .backend_ref = fetched.record.backend_ref,
  };
  return materialized;
}

SecretLifecycleResult SecretManagerFacade::release(const SecretLease& lease) {
  if (!lease.is_valid()) {
    return SecretLifecycleResult::failure(resolve_secret_name_from_handle_id(lease.handle_id),
                                          contracts::ResultCode::ValidationFieldMissing,
                                          "secret manager release requires a valid lease",
                                          "secret.release",
                                          std::string(kSecretManagerFacadeSourceRef));
  }

  const auto lease_it = active_leases_.find(lease.lease_id);
  if (lease_it == active_leases_.end()) {
    return make_lifecycle_failure(std::string(),
                                  SecretErrorCode::LeaseExpired,
                                  "secret lease is no longer active during release",
                                  "secret.release",
                                  lease.lease_id);
  }

  const std::string secret_name = lease_it->second.secret_name;
  active_leases_.erase(lease_it);
  return SecretLifecycleResult::success(secret_name, std::string(lease.lease_id));
}

RotationResult SecretManagerFacade::rotate(const RotationRequest& request) {
  if (!request.is_valid()) {
    return RotationResult::failure(request.secret_name,
                                   {},
                                   {},
                                   "audit://secret/rotate/invalid_request",
                                   contracts::ResultCode::ValidationFieldMissing,
                                   "secret manager rotate requires a valid rotation request",
                                   "secret.rotate",
                                   std::string(kSecretManagerFacadeSourceRef),
                                   false);
  }

  if (!backend_ready()) {
    const SecretErrorMapping mapping = error_mapping(SecretErrorCode::BackendUnavailable);
    return RotationResult::failure(request.secret_name,
                                   {},
                                   {},
                                   "audit://secret/rotate/backend_unavailable",
                                   mapping.result_code,
                                   std::string(secret_error_code_name(SecretErrorCode::BackendUnavailable)) +
                                       ": secret backend is unavailable for rotate",
                                   "secret.rotate",
                                   std::string(kSecretManagerFacadeSourceRef),
                                   false);
  }

  return RotationResult::failure(request.secret_name,
                                 {},
                                 {},
                                 "audit://secret/rotate/deferred",
                                 contracts::ResultCode::ToolExecutionFailed,
                                 "secret rotation is intentionally deferred until SEC-TODO-010",
                                 "secret.rotate",
                                 std::string(kSecretManagerFacadeSourceRef),
                                 false);
}

SecretLifecycleResult SecretManagerFacade::revoke(std::string_view secret_name,
                                                  std::string_view reason_code) {
  if (secret_name.empty() || reason_code.empty()) {
    return SecretLifecycleResult::failure(secret_name.empty() ? std::string("unresolved-secret")
                                                              : std::string(secret_name),
                                          contracts::ResultCode::ValidationFieldMissing,
                                          "secret manager revoke requires a non-empty secret_name and reason_code",
                                          "secret.revoke",
                                          std::string(kSecretManagerFacadeSourceRef));
  }

  if (!backend_ready()) {
    return make_lifecycle_failure(std::string(secret_name),
                                  SecretErrorCode::BackendUnavailable,
                                  "secret backend is unavailable for revoke",
                                  "secret.revoke");
  }

  const auto fetched = backend_->fetch_record(make_query(secret_name,
                                                         {},
                                                         "revoke",
                                                         SecretAccessMode::Revoke));
  if (!fetched.ok) {
    return SecretLifecycleResult::failure(std::string(secret_name),
                                          fetched.result_code.value_or(contracts::ResultCode::ProviderTimeout),
                                          extract_error_message(fetched.error_info,
                                                                "secret backend fetch failed during revoke"),
                                          "secret.revoke",
                                          std::string(kSecretManagerFacadeSourceRef));
  }

  const auto revoked = backend_->revoke_version(secret_name, fetched.record.version);
  if (!revoked.ok) {
    return SecretLifecycleResult::failure(std::string(secret_name),
                                          revoked.result_code.value_or(contracts::ResultCode::ToolExecutionFailed),
                                          extract_error_message(revoked.error_info,
                                                                "secret backend revoke failed"),
                                          "secret.revoke",
                                          std::string(kSecretManagerFacadeSourceRef),
                                          revoked.lease_id);
  }

  cached_secrets_.erase(std::string(secret_name));
  for (auto lease_it = active_leases_.begin(); lease_it != active_leases_.end();) {
    if (lease_it->second.secret_name == secret_name) {
      lease_it = active_leases_.erase(lease_it);
      continue;
    }
    ++lease_it;
  }

  return SecretLifecycleResult::success(std::string(secret_name));
}

SecretInspectionResult SecretManagerFacade::inspect(std::string_view secret_name) const {
  if (secret_name.empty()) {
    return SecretInspectionResult::failure(contracts::ResultCode::ValidationFieldMissing,
                                           "secret manager inspect requires a non-empty secret_name",
                                           "secret.inspect",
                                           std::string(kSecretManagerFacadeSourceRef));
  }

  if (!backend_ready()) {
    return make_inspection_failure(SecretErrorCode::BackendUnavailable,
                                   "secret backend is unavailable for inspect",
                                   "secret.inspect");
  }

  const auto fetched = backend_->fetch_record(make_query(secret_name,
                                                         {},
                                                         "inspect",
                                                         SecretAccessMode::MetadataOnly));
  if (!fetched.ok) {
    return SecretInspectionResult::failure(
        fetched.result_code.value_or(contracts::ResultCode::ProviderTimeout),
        extract_error_message(fetched.error_info,
                              "secret backend fetch failed during inspect"),
        "secret.inspect",
        std::string(kSecretManagerFacadeSourceRef));
  }

  return SecretInspectionResult::success(fetched.record.descriptor);
}

bool SecretManagerFacade::backend_ready() const {
  return backend_ != nullptr &&
         backend_->get_backend_status().state != SecretBackendState::Unavailable;
}

}  // namespace dasall::infra::secret