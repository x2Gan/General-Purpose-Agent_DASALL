#include "AccessErrors.h"

namespace {

using dasall::access::AccessErrorCode;
using dasall::access::AccessProtocolErrorMapping;

[[nodiscard]] constexpr AccessProtocolErrorMapping make_protocol_mapping(
    const AccessErrorCode code,
    const int http_status,
    const int cli_exit_code,
    const std::string_view grpc_status,
    const std::string_view reason,
    const std::string_view http_header_name = {},
    const std::string_view http_header_value = {}) {
  return AccessProtocolErrorMapping{
      .code = code,
      .http_status = http_status,
      .cli_exit_code = cli_exit_code,
      .grpc_status = grpc_status,
      .http_header_name = http_header_name,
      .http_header_value = http_header_value,
      .reason = reason,
  };
}

}  // namespace

namespace dasall::access {

AccessProtocolErrorMapping map_access_error(const AccessErrorCode code) {
  switch (code) {
    case AccessErrorCode::ValidationRejected:
      return make_protocol_mapping(
          code,
          400,
          1,
          "INVALID_ARGUMENT",
          "generic access validation failures stay inside bad-request semantics");
    case AccessErrorCode::PayloadTooLarge:
      return make_protocol_mapping(
          code,
          413,
          1,
          "INVALID_ARGUMENT",
          "oversized payloads map to payload-too-large semantics");
    case AccessErrorCode::UnsupportedProtocol:
      return make_protocol_mapping(
          code,
          400,
          1,
          "INVALID_ARGUMENT",
          "unsupported protocol kinds stay inside access validation semantics");
    case AccessErrorCode::MalformedInput:
      return make_protocol_mapping(
          code,
          400,
          1,
          "INVALID_ARGUMENT",
          "malformed access input stays inside bad-request semantics");
    case AccessErrorCode::AuthenticationFailed:
      return make_protocol_mapping(
          code,
          401,
          77,
          "UNAUTHENTICATED",
          "authentication failures require an unauthenticated response");
    case AccessErrorCode::AuthenticationChallengeRequired:
      return make_protocol_mapping(
          code,
          401,
          77,
          "UNAUTHENTICATED",
          "challenge-required responses must carry a WWW-Authenticate hint",
          "WWW-Authenticate",
          "challenge-required");
    case AccessErrorCode::CredentialExpired:
      return make_protocol_mapping(
          code,
          401,
          77,
          "UNAUTHENTICATED",
          "expired credentials stay inside unauthenticated semantics");
    case AccessErrorCode::AuthorizationDenied:
      return make_protocol_mapping(
          code,
          403,
          77,
          "PERMISSION_DENIED",
          "authorization denials stay inside forbidden semantics");
    case AccessErrorCode::ConfirmationRequired:
      return make_protocol_mapping(
          code,
          403,
          77,
          "PERMISSION_DENIED",
          "confirmation-required responses must surface an explicit confirmation hint",
          "X-Confirmation-Required",
          "true");
    case AccessErrorCode::OverrideSourceInvalid:
      return make_protocol_mapping(
          code,
          403,
          77,
          "PERMISSION_DENIED",
          "override source violations stay inside forbidden semantics");
    case AccessErrorCode::AdmissionRejected:
      return make_protocol_mapping(
          code,
          503,
          75,
          "UNAVAILABLE",
          "generic admission rejects are treated as temporary service unavailability");
    case AccessErrorCode::RateLimitExceeded:
      return make_protocol_mapping(
          code,
          429,
          75,
          "RESOURCE_EXHAUSTED",
          "rate-limit rejects stay inside resource-exhausted semantics");
    case AccessErrorCode::ConcurrencyLimitExceeded:
      return make_protocol_mapping(
          code,
          429,
          75,
          "RESOURCE_EXHAUSTED",
          "concurrency rejects stay inside resource-exhausted semantics");
    case AccessErrorCode::IdempotencyConflict:
      return make_protocol_mapping(
          code,
          409,
          75,
          "ABORTED",
          "idempotency conflicts stay inside conflict semantics");
    case AccessErrorCode::IdempotencyReplayHit:
      return make_protocol_mapping(
          code,
          200,
          0,
          "OK",
          "idempotency replay hits must preserve the prior success semantics",
          "X-Replay-Hit",
          "true");
    case AccessErrorCode::QueueFull:
      return make_protocol_mapping(
          code,
          503,
          75,
          "UNAVAILABLE",
          "queue saturation stays inside temporary unavailability semantics");
    case AccessErrorCode::RuntimeDispatchFailed:
      return make_protocol_mapping(
          code,
          502,
          1,
          "INTERNAL",
          "runtime dispatch failures stay inside bad-gateway semantics");
    case AccessErrorCode::RuntimeDispatchTimeout:
      return make_protocol_mapping(
          code,
          504,
          75,
          "DEADLINE_EXCEEDED",
          "runtime dispatch timeouts stay inside deadline-exceeded semantics");
    case AccessErrorCode::RuntimeBridgeUnavailable:
      return make_protocol_mapping(
          code,
          503,
          75,
          "UNAVAILABLE",
          "runtime bridge unavailability stays inside temporary unavailable semantics");
    case AccessErrorCode::PublishChannelUnavailable:
      return make_protocol_mapping(
          code,
          502,
          1,
          "INTERNAL",
          "publish channel failures stay inside bad-gateway semantics");
    case AccessErrorCode::PublishTimeout:
      return make_protocol_mapping(
          code,
          504,
          75,
          "DEADLINE_EXCEEDED",
          "publish timeouts stay inside deadline-exceeded semantics");
    case AccessErrorCode::PublishEncodingFailed:
      return make_protocol_mapping(
          code,
          500,
          1,
          "INTERNAL",
          "publish encoding failures stay inside internal error semantics");
    case AccessErrorCode::ReceiptNotFound:
      return make_protocol_mapping(
          code,
          404,
          1,
          "NOT_FOUND",
          "receipt lookups should surface not-found semantics");
    case AccessErrorCode::ReceiptExpired:
      return make_protocol_mapping(
          code,
          410,
          1,
          "NOT_FOUND",
          "expired receipts should surface gone semantics");
    case AccessErrorCode::ReceiptOwnerMismatch:
      return make_protocol_mapping(
          code,
          403,
          77,
          "PERMISSION_DENIED",
          "receipt owner mismatches stay inside forbidden semantics");
    case AccessErrorCode::CancellationFailed:
      return make_protocol_mapping(
          code,
          409,
          75,
          "ABORTED",
          "cancellation failures stay inside conflict/aborted semantics");
    case AccessErrorCode::ShuttingDown:
      return make_protocol_mapping(
          code,
          503,
          75,
          "UNAVAILABLE",
          "shutdown rejects stay inside temporary unavailable semantics");
    case AccessErrorCode::InternalError:
      return make_protocol_mapping(
          code,
          500,
          1,
          "INTERNAL",
          "internal access failures stay inside generic internal-error semantics");
  }

  return make_protocol_mapping(
      code,
      500,
      1,
      "INTERNAL",
      "unknown access errors fall back to generic internal-error semantics");
}

}  // namespace dasall::access
