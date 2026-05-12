#pragma once

#include <array>
#include <optional>
#include <string_view>

#include "AccessErrors.h"

namespace dasall::apps::cli {

[[nodiscard]] inline std::optional<dasall::access::AccessErrorCode>
classify_access_error_ref(std::string_view error_ref) {
  using dasall::access::AccessErrorCode;
  using dasall::access::access_error_code_name;

  if (error_ref.empty()) {
    return std::nullopt;
  }

  constexpr std::array<AccessErrorCode, 28> kKnownAccessCodes{{
      AccessErrorCode::ValidationRejected,
      AccessErrorCode::PayloadTooLarge,
      AccessErrorCode::UnsupportedProtocol,
      AccessErrorCode::MalformedInput,
      AccessErrorCode::AuthenticationFailed,
      AccessErrorCode::AuthenticationChallengeRequired,
      AccessErrorCode::CredentialExpired,
      AccessErrorCode::AuthorizationDenied,
      AccessErrorCode::ConfirmationRequired,
      AccessErrorCode::OverrideSourceInvalid,
      AccessErrorCode::AdmissionRejected,
      AccessErrorCode::RateLimitExceeded,
      AccessErrorCode::ConcurrencyLimitExceeded,
      AccessErrorCode::IdempotencyConflict,
      AccessErrorCode::IdempotencyReplayHit,
      AccessErrorCode::QueueFull,
      AccessErrorCode::RuntimeDispatchFailed,
      AccessErrorCode::RuntimeDispatchTimeout,
      AccessErrorCode::RuntimeBridgeUnavailable,
      AccessErrorCode::PublishChannelUnavailable,
      AccessErrorCode::PublishTimeout,
      AccessErrorCode::PublishEncodingFailed,
      AccessErrorCode::ReceiptNotFound,
      AccessErrorCode::ReceiptExpired,
      AccessErrorCode::ReceiptOwnerMismatch,
      AccessErrorCode::CancellationFailed,
      AccessErrorCode::InternalError,
      AccessErrorCode::ShuttingDown,
  }};

  for (const auto code : kKnownAccessCodes) {
    if (error_ref == access_error_code_name(code)) {
      return code;
    }
  }

  if (error_ref == "payload_size_limit_exceeded" ||
      error_ref == "unknown_command" || error_ref == "diag_command_missing" ||
      error_ref == "knowledge_payload_invalid") {
    return AccessErrorCode::ValidationRejected;
  }

  if (error_ref == "authentication_required") {
    return AccessErrorCode::AuthenticationChallengeRequired;
  }

  if (error_ref == "diag_disabled") {
    return AccessErrorCode::AuthorizationDenied;
  }

  if (error_ref == "status_missing" || error_ref == "cancel_missing") {
    return AccessErrorCode::ReceiptNotFound;
  }

  if (error_ref == "status_expired" || error_ref == "cancel_expired") {
    return AccessErrorCode::ReceiptExpired;
  }

  if (error_ref == "status_owner_mismatch" ||
      error_ref == "cancel_owner_mismatch") {
    return AccessErrorCode::ReceiptOwnerMismatch;
  }

  if (error_ref == "status_cancel_forward_failed" ||
      error_ref == "cancel_forward_failed") {
    return AccessErrorCode::CancellationFailed;
  }

  if (error_ref == "daemon_not_ready" ||
      error_ref == "gateway_not_ready_or_shutting_down") {
    return AccessErrorCode::ShuttingDown;
  }

  if (error_ref == "submit_pipeline_not_configured") {
    return AccessErrorCode::RuntimeDispatchFailed;
  }

  if (error_ref == "knowledge_unavailable" ||
      error_ref == "knowledge_refresh_busy" ||
      error_ref == "knowledge_refresh_failed" ||
      error_ref == "knowledge_retrieve_failed") {
    return AccessErrorCode::RuntimeDispatchFailed;
  }

  if (error_ref == "cancel_unexpected_state") {
    return AccessErrorCode::InternalError;
  }

  return std::nullopt;
}

}  // namespace dasall::apps::cli