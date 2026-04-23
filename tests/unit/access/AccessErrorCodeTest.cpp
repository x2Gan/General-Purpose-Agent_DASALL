#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <set>
#include <string>
#include <type_traits>

#include "AccessErrors.h"
#include "support/TestAssertions.h"

namespace {

using dasall::access::AccessError;
using dasall::access::AccessErrorCode;
using dasall::access::AccessErrorDomain;
using dasall::access::access_error_code_name;
using dasall::access::access_error_domain_name;
using dasall::access::classify_access_error_code;
using dasall::access::classify_access_error_code_value;
using dasall::access::describe_access_error;
using dasall::access::is_known_access_error_code;
using dasall::access::make_access_error;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

struct ErrorExpectation {
  AccessErrorCode code;
  int value;
  std::string_view name;
  AccessErrorDomain domain;
  bool retryable;
};

static_assert(std::is_same_v<std::underlying_type_t<AccessErrorCode>, std::uint16_t>);
static_assert(std::is_same_v<decltype(AccessError{}.upstream_error),
                             std::optional<dasall::contracts::ErrorInfo>>);

void test_access_error_code_names_and_ranges_are_stable() {
  constexpr std::array<ErrorExpectation, 28> kExpectations{{
      {AccessErrorCode::ValidationRejected, 100, "validation_rejected",
       AccessErrorDomain::Validation, false},
      {AccessErrorCode::PayloadTooLarge, 101, "payload_too_large",
       AccessErrorDomain::Validation, false},
      {AccessErrorCode::UnsupportedProtocol, 102, "unsupported_protocol",
       AccessErrorDomain::Validation, false},
      {AccessErrorCode::MalformedInput, 103, "malformed_input",
       AccessErrorDomain::Validation, false},
      {AccessErrorCode::AuthenticationFailed, 200, "authentication_failed",
       AccessErrorDomain::Authentication, true},
      {AccessErrorCode::AuthenticationChallengeRequired, 201,
       "authentication_challenge_required", AccessErrorDomain::Authentication, true},
      {AccessErrorCode::CredentialExpired, 202, "credential_expired",
       AccessErrorDomain::Authentication, true},
      {AccessErrorCode::AuthorizationDenied, 300, "authorization_denied",
       AccessErrorDomain::Authorization, false},
      {AccessErrorCode::ConfirmationRequired, 301, "confirmation_required",
       AccessErrorDomain::Authorization, true},
      {AccessErrorCode::OverrideSourceInvalid, 302, "override_source_invalid",
       AccessErrorDomain::Authorization, false},
      {AccessErrorCode::AdmissionRejected, 400, "admission_rejected",
       AccessErrorDomain::Admission, true},
      {AccessErrorCode::RateLimitExceeded, 401, "rate_limit_exceeded",
       AccessErrorDomain::Admission, true},
      {AccessErrorCode::ConcurrencyLimitExceeded, 402, "concurrency_limit_exceeded",
       AccessErrorDomain::Admission, true},
      {AccessErrorCode::IdempotencyConflict, 403, "idempotency_conflict",
       AccessErrorDomain::Admission, true},
      {AccessErrorCode::IdempotencyReplayHit, 404, "idempotency_replay_hit",
       AccessErrorDomain::Admission, false},
      {AccessErrorCode::QueueFull, 405, "queue_full", AccessErrorDomain::Admission, true},
      {AccessErrorCode::RuntimeDispatchFailed, 500, "runtime_dispatch_failed",
       AccessErrorDomain::RuntimeDispatch, false},
      {AccessErrorCode::RuntimeDispatchTimeout, 501, "runtime_dispatch_timeout",
       AccessErrorDomain::RuntimeDispatch, true},
      {AccessErrorCode::RuntimeBridgeUnavailable, 502, "runtime_bridge_unavailable",
       AccessErrorDomain::RuntimeDispatch, true},
      {AccessErrorCode::PublishChannelUnavailable, 600, "publish_channel_unavailable",
       AccessErrorDomain::Publish, true},
      {AccessErrorCode::PublishTimeout, 601, "publish_timeout",
       AccessErrorDomain::Publish, true},
      {AccessErrorCode::PublishEncodingFailed, 602, "publish_encoding_failed",
       AccessErrorDomain::Publish, false},
      {AccessErrorCode::ReceiptNotFound, 700, "receipt_not_found",
       AccessErrorDomain::Receipt, false},
      {AccessErrorCode::ReceiptExpired, 701, "receipt_expired",
       AccessErrorDomain::Receipt, false},
      {AccessErrorCode::ReceiptOwnerMismatch, 702, "receipt_owner_mismatch",
       AccessErrorDomain::Receipt, false},
      {AccessErrorCode::CancellationFailed, 703, "cancellation_failed",
       AccessErrorDomain::Receipt, true},
      {AccessErrorCode::InternalError, 900, "internal_error",
       AccessErrorDomain::Internal, false},
      {AccessErrorCode::ShuttingDown, 901, "shutting_down",
       AccessErrorDomain::Internal, true},
  }};

  std::set<int> unique_values;
  for (const auto& expectation : kExpectations) {
    unique_values.insert(expectation.value);
    assert_equal(expectation.value,
                 static_cast<int>(expectation.code),
                 "access error code value should remain stable");
    assert_equal(std::string(expectation.name),
                 std::string(access_error_code_name(expectation.code)),
                 "access error code name should remain stable");
    assert_true(classify_access_error_code(expectation.code) == expectation.domain,
                "access error code should remain in the frozen domain range");

    const auto descriptor = describe_access_error(expectation.code);
    assert_true(descriptor.domain == expectation.domain,
                "access error descriptor should preserve the frozen domain");
    assert_true(descriptor.retryable == expectation.retryable,
                "access error descriptor should preserve the frozen retryable semantics");
    assert_equal(std::string(expectation.name),
                 std::string(descriptor.default_reason),
                 "default reason should stay aligned with the code name");
  }

  assert_equal(static_cast<int>(kExpectations.size()),
               static_cast<int>(unique_values.size()),
               "access error code values must remain unique");
  assert_equal("runtime_dispatch",
               std::string(access_error_domain_name(AccessErrorDomain::RuntimeDispatch)),
               "access error domain name should remain stable");
  assert_true(classify_access_error_code_value(850) == AccessErrorDomain::Unknown,
              "undefined 8xx codes should remain outside the frozen access taxonomy");
  assert_true(is_known_access_error_code(901),
              "known 9xx codes should remain discoverable through the helper");
  assert_true(!is_known_access_error_code(42),
              "unknown access error codes must stay rejected");
}

void test_make_access_error_uses_descriptor_defaults_and_preserves_upstream_error() {
  const dasall::contracts::ErrorInfo upstream_error{
      .failure_type = dasall::contracts::ResultCodeCategory::Runtime,
      .retryable = true,
      .safe_to_replan = false,
      .details = {
          .code = 5001,
          .message = "runtime dispatch timeout",
          .stage = "runtime.dispatch",
      },
      .source_ref = {
          .ref_type = "runtime::bridge",
          .ref_id = "dispatch_timeout",
      },
  };

  const auto default_reason_error = make_access_error(
      AccessErrorCode::RuntimeDispatchTimeout,
      "dispatch timed out while waiting for runtime");
  assert_true(default_reason_error.code == AccessErrorCode::RuntimeDispatchTimeout,
              "make_access_error should preserve the requested error code");
  assert_equal("runtime_dispatch_timeout",
               default_reason_error.reason,
               "make_access_error should default the reason to the stable code name");
  assert_equal("dispatch timed out while waiting for runtime",
               default_reason_error.detail,
               "make_access_error should preserve the provided detail");
  assert_true(default_reason_error.retryable,
              "timeout-based access errors should stay retryable by default");
  assert_true(!default_reason_error.upstream_error.has_value(),
              "make_access_error should not fabricate upstream errors");

  const auto explicit_reason_error = make_access_error(
      AccessErrorCode::RuntimeBridgeUnavailable,
      "bridge socket is not ready",
      upstream_error,
      "bridge_unavailable_for_bootstrap");
  assert_equal("bridge_unavailable_for_bootstrap",
               explicit_reason_error.reason,
               "make_access_error should preserve an explicit reason override");
  assert_true(explicit_reason_error.retryable,
              "runtime bridge unavailability should remain retryable");
  assert_true(explicit_reason_error.upstream_error.has_value(),
              "make_access_error should preserve the upstream error payload");
  assert_true(explicit_reason_error.upstream_error->details.code.has_value(),
              "upstream error code should remain present");
  assert_equal(5001,
               *explicit_reason_error.upstream_error->details.code,
               "upstream error code should remain stable through AccessError wrapping");
  assert_equal("runtime::bridge",
               explicit_reason_error.upstream_error->source_ref.ref_type,
               "upstream error source type should remain preserved");
}

}  // namespace

int main() {
  try {
    test_access_error_code_names_and_ranges_are_stable();
    test_make_access_error_uses_descriptor_defaults_and_preserves_upstream_error();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}
