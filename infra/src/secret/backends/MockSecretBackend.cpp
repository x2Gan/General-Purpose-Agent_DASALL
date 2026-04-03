#include "MockSecretBackend.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <utility>

#include "secret/SecretErrors.h"

namespace dasall::infra::secret {
namespace {

constexpr std::string_view kMockSecretBackendSourceRef = "MockSecretBackend";

[[nodiscard]] std::int64_t current_time_unix_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

[[nodiscard]] std::string make_handle_id(const SecretBackendRecord& record) {
  return "mock-handle://" + record.descriptor.secret_name + "/" + record.version;
}

[[nodiscard]] std::string make_lease_id(const SecretBackendRecord& record) {
  return "mock-lease://" + record.descriptor.secret_name + "/" + record.version;
}

[[nodiscard]] std::string make_status_detail_ref(std::string_view backend_ref,
                                                 std::string_view suffix) {
  return "status://secret/backend/" + std::string(backend_ref) + "/" + std::string(suffix);
}

[[nodiscard]] SecretBackendFetchResult make_fetch_failure(SecretErrorCode error_code,
                                                          std::string message,
                                                          std::string stage) {
  const SecretErrorMapping mapping = map_secret_error_code(error_code);
  return SecretBackendFetchResult::failure(
      mapping.result_code,
      std::string(secret_error_code_name(error_code)) + ": " + std::move(message),
      std::move(stage),
      std::string(kMockSecretBackendSourceRef));
}

[[nodiscard]] SecretMaterializationResult make_materialize_failure(SecretErrorCode error_code,
                                                                   std::string message,
                                                                   std::string stage) {
  const SecretErrorMapping mapping = map_secret_error_code(error_code);
  return SecretMaterializationResult::failure(
      mapping.result_code,
      std::string(secret_error_code_name(error_code)) + ": " + std::move(message),
      std::move(stage),
      std::string(kMockSecretBackendSourceRef));
}

[[nodiscard]] SecretLifecycleResult make_lifecycle_failure(std::string secret_name,
                                                           SecretErrorCode error_code,
                                                           std::string message,
                                                           std::string stage) {
  const SecretErrorMapping mapping = map_secret_error_code(error_code);
  return SecretLifecycleResult::failure(
      std::move(secret_name),
      mapping.result_code,
      std::string(secret_error_code_name(error_code)) + ": " + std::move(message),
      std::move(stage),
      std::string(kMockSecretBackendSourceRef));
}

[[nodiscard]] RotationResult make_rotation_failure(std::string secret_name,
                                                   std::string previous_version,
                                                   std::string candidate_version,
                                                   SecretErrorCode error_code,
                                                   std::string message,
                                                   std::string stage) {
  const SecretErrorMapping mapping = map_secret_error_code(error_code);
  return RotationResult::failure(
      std::move(secret_name),
      std::move(previous_version),
      std::move(candidate_version),
      "audit://secret/mock/rotation/failure",
      mapping.result_code,
      std::string(secret_error_code_name(error_code)) + ": " + std::move(message),
      std::move(stage),
      std::string(kMockSecretBackendSourceRef),
      true);
}

[[nodiscard]] bool permission_domain_allowed(
    const MockSecretRecord& secret_record,
    std::string_view permission_domain) {
  return secret_record.allowed_permission_domains.empty() ||
         std::find(secret_record.allowed_permission_domains.begin(),
                   secret_record.allowed_permission_domains.end(),
                   permission_domain) != secret_record.allowed_permission_domains.end();
}

}  // namespace

MockSecretBackend::MockSecretBackend(MockSecretBackendOptions options)
    : options_(std::move(options)) {}

void MockSecretBackend::upsert_secret(MockSecretRecord secret_record) {
  if (secret_record.record.descriptor.backend_type == SecretBackendType::Unspecified) {
    secret_record.record.descriptor.backend_type = SecretBackendType::Mock;
  }

  if (secret_record.record.backend_ref.empty()) {
    secret_record.record.backend_ref = options_.backend_ref;
  }

  if (!secret_record.is_valid()) {
    return;
  }

  records_[secret_record.record.descriptor.secret_name] = std::move(secret_record);
}

void MockSecretBackend::set_available(bool available) {
  options_.available = available;
}

void MockSecretBackend::set_rate_limited(bool rate_limited) {
  options_.rate_limited = rate_limited;
}

void MockSecretBackend::clear() {
  records_.clear();
  last_error_code_.reset();
}

SecretBackendFetchResult MockSecretBackend::fetch_record(const SecretQuery& query) {
  if (!options_.available) {
    last_error_code_ = map_secret_error_code(SecretErrorCode::BackendUnavailable).result_code;
    return make_fetch_failure(SecretErrorCode::BackendUnavailable,
                              "mock backend is unavailable for fetch_record",
                              "secret.fetch_record");
  }

  if (!query.is_valid()) {
    last_error_code_ = contracts::ResultCode::ValidationFieldMissing;
    return SecretBackendFetchResult::failure(contracts::ResultCode::ValidationFieldMissing,
                                             "secret query must satisfy the frozen secret contract",
                                             "secret.fetch_record",
                                             std::string(kMockSecretBackendSourceRef));
  }

  const auto record_it = find_record(query.secret_name);
  if (record_it == records_.end()) {
    last_error_code_ = map_secret_error_code(SecretErrorCode::NotFound).result_code;
    return make_fetch_failure(SecretErrorCode::NotFound,
                              "mock backend does not contain the requested secret",
                              "secret.fetch_record");
  }

  if (!query.version_hint.empty() && record_it->second.record.version != query.version_hint) {
    last_error_code_ = map_secret_error_code(SecretErrorCode::NotFound).result_code;
    return make_fetch_failure(SecretErrorCode::NotFound,
                              "mock backend does not contain the requested secret version",
                              "secret.fetch_record");
  }

  last_error_code_.reset();
  return SecretBackendFetchResult::success(record_it->second.record);
}

SecretMaterializationResult MockSecretBackend::materialize_record(
    const SecretBackendRecord& record,
    const SecretAccessContext& access_context) {
  if (!options_.available) {
    last_error_code_ = map_secret_error_code(SecretErrorCode::BackendUnavailable).result_code;
    return make_materialize_failure(SecretErrorCode::BackendUnavailable,
                                    "mock backend is unavailable for materialize_record",
                                    "secret.materialize_record");
  }

  if (!access_context.is_valid()) {
    last_error_code_ = contracts::ResultCode::ValidationFieldMissing;
    return SecretMaterializationResult::failure(
        contracts::ResultCode::ValidationFieldMissing,
        "secret access context must remain auditable before materialization",
        "secret.materialize_record",
        std::string(kMockSecretBackendSourceRef));
  }

  const auto record_it = find_record(record.descriptor.secret_name);
  if (record_it == records_.end()) {
    last_error_code_ = map_secret_error_code(SecretErrorCode::NotFound).result_code;
    return make_materialize_failure(SecretErrorCode::NotFound,
                                    "mock backend cannot materialize a missing secret",
                                    "secret.materialize_record");
  }

  if (record_it->second.record.version != record.version) {
    last_error_code_ = map_secret_error_code(SecretErrorCode::VersionStale).result_code;
    return make_materialize_failure(SecretErrorCode::VersionStale,
                                    "mock backend detected a stale record version during materialization",
                                    "secret.materialize_record");
  }

  if (!permission_domain_allowed(record_it->second, access_context.permission_domain)) {
    last_error_code_ = map_secret_error_code(SecretErrorCode::AccessDenied).result_code;
    return make_materialize_failure(SecretErrorCode::AccessDenied,
                                    "mock backend denied materialization for the provided permission domain",
                                    "secret.materialize_record");
  }

  const std::int64_t issued_at_ms = current_time_unix_ms();
  const std::int64_t expires_at_ms = issued_at_ms + std::max<std::int64_t>(1, options_.lease_duration_ms);
  last_error_code_.reset();
  return SecretMaterializationResult::success(
      std::make_shared<SecureBuffer>(
          SecureBuffer::from_text_copy(record_it->second.materialized_text)),
      SecretLease{
          .lease_id = make_lease_id(record_it->second.record),
          .handle_id = make_handle_id(record_it->second.record),
          .consumer_ref = access_context.consumer_module,
          .expires_at_ms = expires_at_ms,
          .rotation_epoch = std::max<std::uint64_t>(1, options_.rotation_epoch),
          .state = SecretLeaseState::Active,
      });
}

RotationResult MockSecretBackend::promote_version(
    const SecretVersionPromotionRequest& request) {
  if (!options_.available) {
    last_error_code_ = map_secret_error_code(SecretErrorCode::BackendUnavailable).result_code;
    return make_rotation_failure(request.secret_name,
                                 request.previous_version,
                                 request.candidate_version,
                                 SecretErrorCode::BackendUnavailable,
                                 "mock backend is unavailable for promote_version",
                                 "secret.promote_version");
  }

  if (!request.is_valid()) {
    last_error_code_ = contracts::ResultCode::ValidationFieldMissing;
    return RotationResult::failure(request.secret_name,
                                   request.previous_version,
                                   request.candidate_version,
                                   "audit://secret/mock/rotation/invalid_request",
                                   contracts::ResultCode::ValidationFieldMissing,
                                   "promotion request must satisfy the frozen secret promotion contract",
                                   "secret.promote_version",
                                   std::string(kMockSecretBackendSourceRef),
                                   false);
  }

  const auto record_it = find_record(request.secret_name);
  if (record_it == records_.end()) {
    last_error_code_ = map_secret_error_code(SecretErrorCode::NotFound).result_code;
    return make_rotation_failure(request.secret_name,
                                 request.previous_version,
                                 request.candidate_version,
                                 SecretErrorCode::NotFound,
                                 "mock backend cannot promote a missing secret",
                                 "secret.promote_version");
  }

  if (record_it->second.record.version != request.previous_version) {
    last_error_code_ = map_secret_error_code(SecretErrorCode::VersionStale).result_code;
    return make_rotation_failure(request.secret_name,
                                 request.previous_version,
                                 request.candidate_version,
                                 SecretErrorCode::VersionStale,
                                 "mock backend detected a stale previous version during promotion",
                                 "secret.promote_version");
  }

  if (!request.validate_only) {
    record_it->second.record.version = request.candidate_version;
    record_it->second.record.cipher_ref =
        "cipher://mock/" + request.secret_name + "/" + request.candidate_version;
  }

  last_error_code_.reset();
  return RotationResult::success(request.secret_name,
                                 request.previous_version,
                                 request.candidate_version,
                                 "audit://secret/mock/promote/" + request.secret_name,
                                 true);
}

SecretLifecycleResult MockSecretBackend::revoke_version(std::string_view secret_name,
                                                        std::string_view version) {
  if (!options_.available) {
    last_error_code_ = map_secret_error_code(SecretErrorCode::BackendUnavailable).result_code;
    return make_lifecycle_failure(std::string(secret_name),
                                  SecretErrorCode::BackendUnavailable,
                                  "mock backend is unavailable for revoke_version",
                                  "secret.revoke_version");
  }

  if (secret_name.empty() || version.empty()) {
    last_error_code_ = contracts::ResultCode::ValidationFieldMissing;
    return SecretLifecycleResult::failure(std::string(secret_name),
                                          contracts::ResultCode::ValidationFieldMissing,
                                          "revoke_version requires a non-empty secret_name and version",
                                          "secret.revoke_version",
                                          std::string(kMockSecretBackendSourceRef));
  }

  const auto record_it = find_record(secret_name);
  if (record_it == records_.end()) {
    last_error_code_ = map_secret_error_code(SecretErrorCode::NotFound).result_code;
    return make_lifecycle_failure(std::string(secret_name),
                                  SecretErrorCode::NotFound,
                                  "mock backend cannot revoke a missing secret",
                                  "secret.revoke_version");
  }

  if (record_it->second.record.version != version) {
    last_error_code_ = map_secret_error_code(SecretErrorCode::VersionStale).result_code;
    return make_lifecycle_failure(std::string(secret_name),
                                  SecretErrorCode::VersionStale,
                                  "mock backend detected a stale version during revoke",
                                  "secret.revoke_version");
  }

  records_.erase(record_it);
  last_error_code_.reset();
  return SecretLifecycleResult::success(std::string(secret_name));
}

SecretBackendStatus MockSecretBackend::get_backend_status() const {
  const SecretBackendState state = !options_.available
                                       ? SecretBackendState::Unavailable
                                       : (options_.rate_limited ? SecretBackendState::Degraded
                                                                : SecretBackendState::Available);

  const std::string_view suffix = !options_.available
                                      ? "unavailable"
                                      : (options_.rate_limited ? "degraded" : "available");

  return SecretBackendStatus{
      .state = state,
      .backend_ref = options_.backend_ref,
      .rate_limited = options_.rate_limited,
      .read_only_fallback_ready = options_.read_only_fallback_ready,
      .last_error_code = last_error_code_,
      .detail_ref = make_status_detail_ref(options_.backend_ref, suffix),
  };
}

MockSecretBackend::RecordMap::iterator MockSecretBackend::find_record(
    std::string_view secret_name) {
  return records_.find(std::string(secret_name));
}

MockSecretBackend::RecordMap::const_iterator MockSecretBackend::find_record(
    std::string_view secret_name) const {
  return records_.find(std::string(secret_name));
}

}  // namespace dasall::infra::secret