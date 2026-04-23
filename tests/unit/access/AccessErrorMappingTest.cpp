#include <array>
#include <exception>
#include <iostream>
#include <string>
#include <string_view>
#include <type_traits>

#include "AccessErrors.h"
#include "support/TestAssertions.h"

namespace {

using dasall::access::AccessErrorCode;
using dasall::access::AccessProtocolErrorMapping;
using dasall::access::map_access_error;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

struct MappingExpectation {
  AccessErrorCode code;
  int http_status;
  int cli_exit_code;
  std::string_view grpc_status;
  std::string_view http_header_name;
  std::string_view http_header_value;
};

static_assert(std::is_same_v<decltype(AccessProtocolErrorMapping{}.http_status), int>);
static_assert(std::is_same_v<decltype(AccessProtocolErrorMapping{}.cli_exit_code), int>);
static_assert(
    std::is_same_v<decltype(AccessProtocolErrorMapping{}.grpc_status), std::string_view>);

void test_access_error_mapping_matches_the_frozen_protocol_matrix() {
  constexpr std::array<MappingExpectation, 28> kExpectations{{
      {AccessErrorCode::ValidationRejected, 400, 1, "INVALID_ARGUMENT", "", ""},
      {AccessErrorCode::PayloadTooLarge, 413, 1, "INVALID_ARGUMENT", "", ""},
      {AccessErrorCode::UnsupportedProtocol, 400, 1, "INVALID_ARGUMENT", "", ""},
      {AccessErrorCode::MalformedInput, 400, 1, "INVALID_ARGUMENT", "", ""},
      {AccessErrorCode::AuthenticationFailed, 401, 77, "UNAUTHENTICATED", "", ""},
      {AccessErrorCode::AuthenticationChallengeRequired, 401, 77, "UNAUTHENTICATED",
       "WWW-Authenticate", "challenge-required"},
      {AccessErrorCode::CredentialExpired, 401, 77, "UNAUTHENTICATED", "", ""},
      {AccessErrorCode::AuthorizationDenied, 403, 77, "PERMISSION_DENIED", "", ""},
      {AccessErrorCode::ConfirmationRequired, 403, 77, "PERMISSION_DENIED",
       "X-Confirmation-Required", "true"},
      {AccessErrorCode::OverrideSourceInvalid, 403, 77, "PERMISSION_DENIED", "", ""},
      {AccessErrorCode::AdmissionRejected, 503, 75, "UNAVAILABLE", "", ""},
      {AccessErrorCode::RateLimitExceeded, 429, 75, "RESOURCE_EXHAUSTED", "", ""},
      {AccessErrorCode::ConcurrencyLimitExceeded, 429, 75, "RESOURCE_EXHAUSTED", "", ""},
      {AccessErrorCode::IdempotencyConflict, 409, 75, "ABORTED", "", ""},
      {AccessErrorCode::IdempotencyReplayHit, 200, 0, "OK", "X-Replay-Hit", "true"},
      {AccessErrorCode::QueueFull, 503, 75, "UNAVAILABLE", "", ""},
      {AccessErrorCode::RuntimeDispatchFailed, 502, 1, "INTERNAL", "", ""},
      {AccessErrorCode::RuntimeDispatchTimeout, 504, 75, "DEADLINE_EXCEEDED", "", ""},
      {AccessErrorCode::RuntimeBridgeUnavailable, 503, 75, "UNAVAILABLE", "", ""},
      {AccessErrorCode::PublishChannelUnavailable, 502, 1, "INTERNAL", "", ""},
      {AccessErrorCode::PublishTimeout, 504, 75, "DEADLINE_EXCEEDED", "", ""},
      {AccessErrorCode::PublishEncodingFailed, 500, 1, "INTERNAL", "", ""},
      {AccessErrorCode::ReceiptNotFound, 404, 1, "NOT_FOUND", "", ""},
      {AccessErrorCode::ReceiptExpired, 410, 1, "NOT_FOUND", "", ""},
      {AccessErrorCode::ReceiptOwnerMismatch, 403, 77, "PERMISSION_DENIED", "", ""},
      {AccessErrorCode::CancellationFailed, 409, 75, "ABORTED", "", ""},
      {AccessErrorCode::InternalError, 500, 1, "INTERNAL", "", ""},
      {AccessErrorCode::ShuttingDown, 503, 75, "UNAVAILABLE", "", ""},
  }};

  for (const auto& expectation : kExpectations) {
    const auto mapping = map_access_error(expectation.code);
    assert_equal(expectation.http_status,
                 mapping.http_status,
                 "access protocol mapping should preserve the frozen HTTP status");
    assert_equal(expectation.cli_exit_code,
                 mapping.cli_exit_code,
                 "access protocol mapping should preserve the frozen CLI exit code");
    assert_equal(std::string(expectation.grpc_status),
                 std::string(mapping.grpc_status),
                 "access protocol mapping should preserve the frozen gRPC status");
    assert_equal(std::string(expectation.http_header_name),
                 std::string(mapping.http_header_name),
                 "access protocol mapping should preserve the frozen header name hint");
    assert_equal(std::string(expectation.http_header_value),
                 std::string(mapping.http_header_value),
                 "access protocol mapping should preserve the frozen header value hint");
    assert_true(!mapping.reason.empty(),
                "each frozen access error mapping should carry an observable rationale");
  }
}

void test_access_error_mapping_keeps_special_success_and_security_hints() {
  const auto replay_hit = map_access_error(AccessErrorCode::IdempotencyReplayHit);
  assert_equal(200,
               replay_hit.http_status,
               "idempotency replay hits should keep their success-like HTTP status");
  assert_equal(0,
               replay_hit.cli_exit_code,
               "idempotency replay hits should keep a success-like CLI exit code");
  assert_equal("X-Replay-Hit",
               std::string(replay_hit.http_header_name),
               "idempotency replay hits should surface the replay hint header");

  const auto challenge_required =
      map_access_error(AccessErrorCode::AuthenticationChallengeRequired);
  assert_equal("WWW-Authenticate",
               std::string(challenge_required.http_header_name),
               "challenge-required errors should keep the auth challenge header");

  const auto confirmation_required =
      map_access_error(AccessErrorCode::ConfirmationRequired);
  assert_equal("X-Confirmation-Required",
               std::string(confirmation_required.http_header_name),
               "confirmation-required errors should keep the confirmation hint header");
}

}  // namespace

int main() {
  try {
    test_access_error_mapping_matches_the_frozen_protocol_matrix();
    test_access_error_mapping_keeps_special_success_and_security_hints();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}
