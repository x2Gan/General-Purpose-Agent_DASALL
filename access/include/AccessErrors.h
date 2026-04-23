#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "error/ErrorInfo.h"

namespace dasall::access {

enum class AccessErrorDomain : std::uint16_t {
  Unknown = 0,
  Validation = 1,
  Authentication = 2,
  Authorization = 3,
  Admission = 4,
  RuntimeDispatch = 5,
  Publish = 6,
  Receipt = 7,
  Internal = 9,
};

enum class AccessErrorCode : std::uint16_t {
  ValidationRejected = 100,
  PayloadTooLarge = 101,
  UnsupportedProtocol = 102,
  MalformedInput = 103,

  AuthenticationFailed = 200,
  AuthenticationChallengeRequired = 201,
  CredentialExpired = 202,

  AuthorizationDenied = 300,
  ConfirmationRequired = 301,
  OverrideSourceInvalid = 302,

  AdmissionRejected = 400,
  RateLimitExceeded = 401,
  ConcurrencyLimitExceeded = 402,
  IdempotencyConflict = 403,
  IdempotencyReplayHit = 404,
  QueueFull = 405,

  RuntimeDispatchFailed = 500,
  RuntimeDispatchTimeout = 501,
  RuntimeBridgeUnavailable = 502,

  PublishChannelUnavailable = 600,
  PublishTimeout = 601,
  PublishEncodingFailed = 602,

  ReceiptNotFound = 700,
  ReceiptExpired = 701,
  ReceiptOwnerMismatch = 702,
  CancellationFailed = 703,

  InternalError = 900,
  ShuttingDown = 901,
};

struct AccessError {
  AccessErrorCode code = AccessErrorCode::InternalError;
  std::string reason;
  std::string detail;
  bool retryable = false;
  std::optional<dasall::contracts::ErrorInfo> upstream_error;
};

struct AccessErrorDescriptor {
  AccessErrorDomain domain = AccessErrorDomain::Unknown;
  bool retryable = false;
  std::string_view default_reason = "internal_error";
};

struct AccessProtocolErrorMapping {
  AccessErrorCode code = AccessErrorCode::InternalError;
  int http_status = 500;
  int cli_exit_code = 1;
  std::string_view grpc_status = "INTERNAL";
  std::string_view http_header_name;
  std::string_view http_header_value;
  std::string_view reason = "unknown access error falls back to INTERNAL";
};

[[nodiscard]] inline constexpr std::string_view access_error_domain_name(
    const AccessErrorDomain domain) {
  switch (domain) {
    case AccessErrorDomain::Validation:
      return "validation";
    case AccessErrorDomain::Authentication:
      return "authentication";
    case AccessErrorDomain::Authorization:
      return "authorization";
    case AccessErrorDomain::Admission:
      return "admission";
    case AccessErrorDomain::RuntimeDispatch:
      return "runtime_dispatch";
    case AccessErrorDomain::Publish:
      return "publish";
    case AccessErrorDomain::Receipt:
      return "receipt";
    case AccessErrorDomain::Internal:
      return "internal";
    case AccessErrorDomain::Unknown:
      return "unknown";
  }

  return "unknown";
}

[[nodiscard]] inline constexpr AccessErrorDomain classify_access_error_code_value(
    const int raw_code) {
  if (raw_code >= 100 && raw_code <= 199) {
    return AccessErrorDomain::Validation;
  }

  if (raw_code >= 200 && raw_code <= 299) {
    return AccessErrorDomain::Authentication;
  }

  if (raw_code >= 300 && raw_code <= 399) {
    return AccessErrorDomain::Authorization;
  }

  if (raw_code >= 400 && raw_code <= 499) {
    return AccessErrorDomain::Admission;
  }

  if (raw_code >= 500 && raw_code <= 599) {
    return AccessErrorDomain::RuntimeDispatch;
  }

  if (raw_code >= 600 && raw_code <= 699) {
    return AccessErrorDomain::Publish;
  }

  if (raw_code >= 700 && raw_code <= 799) {
    return AccessErrorDomain::Receipt;
  }

  if (raw_code >= 900 && raw_code <= 999) {
    return AccessErrorDomain::Internal;
  }

  return AccessErrorDomain::Unknown;
}

[[nodiscard]] inline constexpr AccessErrorDomain classify_access_error_code(
    const AccessErrorCode code) {
  return classify_access_error_code_value(static_cast<int>(code));
}

[[nodiscard]] inline constexpr bool is_known_access_error_code(const int raw_code) {
  return classify_access_error_code_value(raw_code) != AccessErrorDomain::Unknown;
}

[[nodiscard]] inline constexpr std::string_view access_error_code_name(
    const AccessErrorCode code) {
  switch (code) {
    case AccessErrorCode::ValidationRejected:
      return "validation_rejected";
    case AccessErrorCode::PayloadTooLarge:
      return "payload_too_large";
    case AccessErrorCode::UnsupportedProtocol:
      return "unsupported_protocol";
    case AccessErrorCode::MalformedInput:
      return "malformed_input";
    case AccessErrorCode::AuthenticationFailed:
      return "authentication_failed";
    case AccessErrorCode::AuthenticationChallengeRequired:
      return "authentication_challenge_required";
    case AccessErrorCode::CredentialExpired:
      return "credential_expired";
    case AccessErrorCode::AuthorizationDenied:
      return "authorization_denied";
    case AccessErrorCode::ConfirmationRequired:
      return "confirmation_required";
    case AccessErrorCode::OverrideSourceInvalid:
      return "override_source_invalid";
    case AccessErrorCode::AdmissionRejected:
      return "admission_rejected";
    case AccessErrorCode::RateLimitExceeded:
      return "rate_limit_exceeded";
    case AccessErrorCode::ConcurrencyLimitExceeded:
      return "concurrency_limit_exceeded";
    case AccessErrorCode::IdempotencyConflict:
      return "idempotency_conflict";
    case AccessErrorCode::IdempotencyReplayHit:
      return "idempotency_replay_hit";
    case AccessErrorCode::QueueFull:
      return "queue_full";
    case AccessErrorCode::RuntimeDispatchFailed:
      return "runtime_dispatch_failed";
    case AccessErrorCode::RuntimeDispatchTimeout:
      return "runtime_dispatch_timeout";
    case AccessErrorCode::RuntimeBridgeUnavailable:
      return "runtime_bridge_unavailable";
    case AccessErrorCode::PublishChannelUnavailable:
      return "publish_channel_unavailable";
    case AccessErrorCode::PublishTimeout:
      return "publish_timeout";
    case AccessErrorCode::PublishEncodingFailed:
      return "publish_encoding_failed";
    case AccessErrorCode::ReceiptNotFound:
      return "receipt_not_found";
    case AccessErrorCode::ReceiptExpired:
      return "receipt_expired";
    case AccessErrorCode::ReceiptOwnerMismatch:
      return "receipt_owner_mismatch";
    case AccessErrorCode::CancellationFailed:
      return "cancellation_failed";
    case AccessErrorCode::InternalError:
      return "internal_error";
    case AccessErrorCode::ShuttingDown:
      return "shutting_down";
  }

  return "internal_error";
}

[[nodiscard]] inline constexpr AccessErrorDescriptor describe_access_error(
    const AccessErrorCode code) {
  switch (code) {
    case AccessErrorCode::ValidationRejected:
    case AccessErrorCode::PayloadTooLarge:
    case AccessErrorCode::UnsupportedProtocol:
    case AccessErrorCode::MalformedInput:
      return AccessErrorDescriptor{
          .domain = AccessErrorDomain::Validation,
          .retryable = false,
          .default_reason = access_error_code_name(code),
      };
    case AccessErrorCode::AuthenticationFailed:
    case AccessErrorCode::AuthenticationChallengeRequired:
    case AccessErrorCode::CredentialExpired:
      return AccessErrorDescriptor{
          .domain = AccessErrorDomain::Authentication,
          .retryable = true,
          .default_reason = access_error_code_name(code),
      };
    case AccessErrorCode::AuthorizationDenied:
    case AccessErrorCode::OverrideSourceInvalid:
      return AccessErrorDescriptor{
          .domain = AccessErrorDomain::Authorization,
          .retryable = false,
          .default_reason = access_error_code_name(code),
      };
    case AccessErrorCode::ConfirmationRequired:
      return AccessErrorDescriptor{
          .domain = AccessErrorDomain::Authorization,
          .retryable = true,
          .default_reason = access_error_code_name(code),
      };
    case AccessErrorCode::AdmissionRejected:
    case AccessErrorCode::RateLimitExceeded:
    case AccessErrorCode::ConcurrencyLimitExceeded:
    case AccessErrorCode::IdempotencyConflict:
    case AccessErrorCode::QueueFull:
      return AccessErrorDescriptor{
          .domain = AccessErrorDomain::Admission,
          .retryable = true,
          .default_reason = access_error_code_name(code),
      };
    case AccessErrorCode::IdempotencyReplayHit:
      return AccessErrorDescriptor{
          .domain = AccessErrorDomain::Admission,
          .retryable = false,
          .default_reason = access_error_code_name(code),
      };
    case AccessErrorCode::RuntimeDispatchFailed:
      return AccessErrorDescriptor{
          .domain = AccessErrorDomain::RuntimeDispatch,
          .retryable = false,
          .default_reason = access_error_code_name(code),
      };
    case AccessErrorCode::RuntimeDispatchTimeout:
    case AccessErrorCode::RuntimeBridgeUnavailable:
      return AccessErrorDescriptor{
          .domain = AccessErrorDomain::RuntimeDispatch,
          .retryable = true,
          .default_reason = access_error_code_name(code),
      };
    case AccessErrorCode::PublishChannelUnavailable:
    case AccessErrorCode::PublishTimeout:
      return AccessErrorDescriptor{
          .domain = AccessErrorDomain::Publish,
          .retryable = true,
          .default_reason = access_error_code_name(code),
      };
    case AccessErrorCode::PublishEncodingFailed:
      return AccessErrorDescriptor{
          .domain = AccessErrorDomain::Publish,
          .retryable = false,
          .default_reason = access_error_code_name(code),
      };
    case AccessErrorCode::ReceiptNotFound:
    case AccessErrorCode::ReceiptExpired:
    case AccessErrorCode::ReceiptOwnerMismatch:
      return AccessErrorDescriptor{
          .domain = AccessErrorDomain::Receipt,
          .retryable = false,
          .default_reason = access_error_code_name(code),
      };
    case AccessErrorCode::CancellationFailed:
      return AccessErrorDescriptor{
          .domain = AccessErrorDomain::Receipt,
          .retryable = true,
          .default_reason = access_error_code_name(code),
      };
    case AccessErrorCode::ShuttingDown:
      return AccessErrorDescriptor{
          .domain = AccessErrorDomain::Internal,
          .retryable = true,
          .default_reason = access_error_code_name(code),
      };
    case AccessErrorCode::InternalError:
      return AccessErrorDescriptor{
          .domain = AccessErrorDomain::Internal,
          .retryable = false,
          .default_reason = access_error_code_name(code),
      };
  }

  return AccessErrorDescriptor{};
}

[[nodiscard]] inline AccessError make_access_error(
    const AccessErrorCode code,
    std::string detail,
    std::optional<dasall::contracts::ErrorInfo> upstream_error = std::nullopt,
    std::string reason = {}) {
  const auto descriptor = describe_access_error(code);
  if (reason.empty()) {
    reason = std::string(descriptor.default_reason);
  }

  return AccessError{
      .code = code,
      .reason = std::move(reason),
      .detail = std::move(detail),
      .retryable = descriptor.retryable,
      .upstream_error = std::move(upstream_error),
  };
}

[[nodiscard]] AccessProtocolErrorMapping map_access_error(AccessErrorCode code);

}  // namespace dasall::access
