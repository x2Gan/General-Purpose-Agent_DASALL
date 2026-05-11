#include "CliExitDecision.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>

#include "AccessErrors.h"
#include "CliAccessErrorProjection.h"

namespace dasall::apps::cli {

namespace {

constexpr int kCliExitSuccess = 0;
constexpr int kCliExitInvalidArguments = 2;
constexpr int kCliExitDaemonUnavailable = 3;
constexpr int kCliExitAccessDenied = 4;
constexpr int kCliExitBusinessFailure = 5;
constexpr int kCliExitRetryableFailure = 6;
constexpr int kCliExitProtocolError = 7;

[[nodiscard]] bool is_json_mode(const CliOutputMode output_mode) {
  return output_mode == CliOutputMode::Json;
}

[[nodiscard]] std::string build_diagnostic_hint(
    const DaemonClientResponse& response,
    std::string_view fallback = {}) {
  if (!response.failure_reason.empty()) {
    return response.failure_reason;
  }

  if (response.error_ref.has_value() && !response.error_ref->empty()) {
    return *response.error_ref;
  }

  if (response.exit_code_hint.has_value()) {
    return std::string("exit_code_hint=") +
           std::to_string(*response.exit_code_hint);
  }

  return std::string(fallback);
}

[[nodiscard]] CliExitDecision make_decision(
    const int exit_code,
    const CliOutcomeFamily outcome_family,
    const bool json_mode,
    std::string diagnostic_hint = {}) {
  return CliExitDecision{
      .exit_code = exit_code,
      .primary_output_stream = exit_code == kCliExitSuccess
                                  ? CliPrimaryOutputStream::Stdout
                                  : CliPrimaryOutputStream::Stderr,
      .json_mode = json_mode,
      .outcome_family = outcome_family,
      .diagnostic_hint = std::move(diagnostic_hint),
  };
}

}  // namespace

CliExitDecision make_argument_error_decision(const CliOutputMode output_mode,
                                             std::string_view diagnostic_hint) {
  return make_decision(kCliExitInvalidArguments,
                       CliOutcomeFamily::InvalidArguments,
                       is_json_mode(output_mode),
                       std::string(diagnostic_hint));
}

CliExitDecision decide_exit_for_response(const DaemonClientResponse& response,
                                         const CliOutputMode output_mode) {
  const bool json_mode = is_json_mode(output_mode);

  if (!response.transport_ok || response.peer_closed) {
    return make_decision(kCliExitDaemonUnavailable,
                         CliOutcomeFamily::DaemonUnavailable,
                         json_mode,
                         build_diagnostic_hint(response, "daemon_unavailable"));
  }

  if (!response.parse_ok) {
    return make_decision(kCliExitProtocolError,
                         CliOutcomeFamily::ProtocolError,
                         json_mode,
                         build_diagnostic_hint(response, "protocol_error"));
  }

  if (response.is_accepted_async() && !response.receipt_ref.has_value()) {
    return make_decision(kCliExitProtocolError,
                         CliOutcomeFamily::ProtocolError,
                         json_mode,
                         "accepted_async missing receipt_ref");
  }

  const auto access_error_code = response.error_ref.has_value()
                                     ? classify_access_error_ref(*response.error_ref)
                                     : std::nullopt;

  if (response.is_completed() && response.task_completed.has_value() &&
      !*response.task_completed && !response.error_ref.has_value()) {
    return make_decision(kCliExitBusinessFailure,
                         CliOutcomeFamily::BusinessFailure,
                         json_mode,
                         build_diagnostic_hint(response, "task_not_completed"));
  }

  if ((response.is_completed() || response.is_accepted_async()) &&
      !response.error_ref.has_value()) {
    return make_decision(kCliExitSuccess,
                         CliOutcomeFamily::Success,
                         json_mode,
                         build_diagnostic_hint(response));
  }

  if (access_error_code == dasall::access::AccessErrorCode::IdempotencyReplayHit) {
    return make_decision(kCliExitSuccess,
                         CliOutcomeFamily::Success,
                         json_mode,
                         build_diagnostic_hint(response));
  }

  if (access_error_code.has_value()) {
    const auto descriptor = dasall::access::describe_access_error(*access_error_code);
    if (descriptor.domain == dasall::access::AccessErrorDomain::Validation) {
      return make_decision(kCliExitInvalidArguments,
                           CliOutcomeFamily::InvalidArguments,
                           json_mode,
                           build_diagnostic_hint(response));
    }

    if (descriptor.domain == dasall::access::AccessErrorDomain::Authentication ||
        descriptor.domain == dasall::access::AccessErrorDomain::Authorization ||
        *access_error_code == dasall::access::AccessErrorCode::ReceiptOwnerMismatch) {
      return make_decision(kCliExitAccessDenied,
                           CliOutcomeFamily::AccessDenied,
                           json_mode,
                           build_diagnostic_hint(response));
    }

    if (response.is_not_ready() || descriptor.retryable) {
      return make_decision(kCliExitRetryableFailure,
                           CliOutcomeFamily::RetryableFailure,
                           json_mode,
                           build_diagnostic_hint(response));
    }

    return make_decision(kCliExitBusinessFailure,
                         CliOutcomeFamily::BusinessFailure,
                         json_mode,
                         build_diagnostic_hint(response));
  }

  if (response.is_not_ready()) {
    return make_decision(kCliExitRetryableFailure,
                         CliOutcomeFamily::RetryableFailure,
                         json_mode,
                         build_diagnostic_hint(response, "not_ready"));
  }

  if (response.disposition ==
      dasall::access::daemon::UdsResponseDisposition::Rejected) {
    return make_decision(kCliExitBusinessFailure,
                         CliOutcomeFamily::BusinessFailure,
                         json_mode,
                         build_diagnostic_hint(response, "daemon_rejected"));
  }

  if (response.is_completed() || response.is_accepted_async()) {
    return make_decision(kCliExitSuccess,
                         CliOutcomeFamily::Success,
                         json_mode,
                         build_diagnostic_hint(response));
  }

  return make_decision(kCliExitProtocolError,
                       CliOutcomeFamily::ProtocolError,
                       json_mode,
                       build_diagnostic_hint(response, "protocol_error"));
}

}  // namespace dasall::apps::cli