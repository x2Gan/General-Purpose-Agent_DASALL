#include <exception>
#include <iostream>
#include <string>

#include "CliExitDecision.h"
#include "support/TestAssertions.h"

namespace {

using dasall::access::daemon::UdsResponseDisposition;
using dasall::apps::cli::CliOutcomeFamily;
using dasall::apps::cli::CliOutputMode;
using dasall::apps::cli::CliPrimaryOutputStream;
using dasall::apps::cli::DaemonClientResponse;
using dasall::apps::cli::decide_exit_for_response;
using dasall::apps::cli::make_argument_error_decision;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] DaemonClientResponse make_response(
    const UdsResponseDisposition disposition,
    std::optional<std::string> error_ref = std::nullopt) {
  DaemonClientResponse response;
  response.transport_ok = true;
  response.parse_ok = true;
  response.disposition = disposition;
  response.error_ref = std::move(error_ref);
  return response;
}

void test_cli_argument_failures_stay_on_exit_code_2() {
  const auto decision = make_argument_error_decision(CliOutputMode::Json);
  assert_equal(2,
               decision.exit_code,
               "parser failures should stay on the frozen CLI exit code 2");
  assert_true(decision.json_mode,
              "argument decisions should preserve the requested json mode flag");
  assert_true(decision.outcome_family == CliOutcomeFamily::InvalidArguments,
              "parser failures should stay in the invalid-arguments family");
  assert_true(decision.primary_output_stream == CliPrimaryOutputStream::Stderr,
              "parser failures should keep stderr as the primary output stream");
}

void test_transport_failures_map_to_daemon_unavailable() {
  DaemonClientResponse response;
  response.failure_reason = "connect timeout";

  const auto decision = decide_exit_for_response(response);
  assert_equal(3,
               decision.exit_code,
               "local transport failures should map to daemon unavailable");
  assert_true(decision.outcome_family == CliOutcomeFamily::DaemonUnavailable,
              "transport failures should stay in daemon-unavailable family");

  response.transport_ok = true;
  response.peer_closed = true;
  const auto peer_closed = decide_exit_for_response(response);
  assert_equal(3,
               peer_closed.exit_code,
               "peer-closed failures should stay in daemon unavailable family");
}

void test_protocol_failures_map_to_exit_code_7() {
  DaemonClientResponse invalid_frame;
  invalid_frame.transport_ok = true;
  invalid_frame.failure_reason = "daemon returned an invalid response frame";

  const auto invalid_frame_decision = decide_exit_for_response(invalid_frame);
  assert_equal(7,
               invalid_frame_decision.exit_code,
               "invalid response frames should map to protocol error exit 7");
  assert_true(invalid_frame_decision.outcome_family == CliOutcomeFamily::ProtocolError,
              "invalid response frames should stay in protocol-error family");

  auto accepted_without_receipt = make_response(UdsResponseDisposition::AcceptedAsync);
  const auto accepted_without_receipt_decision =
      decide_exit_for_response(accepted_without_receipt);
  assert_equal(7,
               accepted_without_receipt_decision.exit_code,
               "accepted_async without receipt_ref should be treated as protocol breakage");
}

void test_success_like_paths_map_to_exit_code_0() {
  const auto completed = decide_exit_for_response(
      make_response(UdsResponseDisposition::Completed), CliOutputMode::Json);
  assert_equal(0,
               completed.exit_code,
               "completed responses should map to success exit code 0");
  assert_true(completed.json_mode,
              "success decisions should preserve json mode for downstream formatter use");
  assert_true(completed.outcome_family == CliOutcomeFamily::Success,
              "completed responses should stay in success family");
  assert_true(completed.primary_output_stream == CliPrimaryOutputStream::Stdout,
              "success responses should keep stdout as primary output stream");

  auto accepted = make_response(UdsResponseDisposition::AcceptedAsync);
  accepted.receipt_ref = std::string("receipt-031");
  const auto accepted_decision = decide_exit_for_response(accepted);
  assert_equal(0,
               accepted_decision.exit_code,
               "accepted_async with receipt_ref should map to success exit code 0");

  const auto replay_hit = decide_exit_for_response(
      make_response(UdsResponseDisposition::Rejected, std::string("idempotency_replay_hit")));
  assert_equal(0,
               replay_hit.exit_code,
               "idempotency replay hits should stay on CLI exit 0 as a success-like replay path");
  assert_true(replay_hit.outcome_family == CliOutcomeFamily::Success,
              "idempotency replay hits should stay in the success family");
}

void test_validation_and_access_denials_keep_the_frozen_cli_families() {
  const auto validation = decide_exit_for_response(
      make_response(UdsResponseDisposition::Rejected, std::string("payload_size_limit_exceeded")));
  assert_equal(2,
               validation.exit_code,
               "validation-style daemon rejects should stay on CLI exit 2");

  const auto access_deny = decide_exit_for_response(
      make_response(UdsResponseDisposition::Rejected, std::string("authorization_denied")));
  assert_equal(4,
               access_deny.exit_code,
               "authorization denies should stay on CLI exit 4");
  assert_true(access_deny.outcome_family == CliOutcomeFamily::AccessDenied,
              "authorization denies should stay in access-denied family");

  const auto owner_mismatch = decide_exit_for_response(
      make_response(UdsResponseDisposition::Rejected, std::string("status_owner_mismatch")));
  assert_equal(4,
               owner_mismatch.exit_code,
               "receipt ownership mismatches should stay on CLI exit 4");

    const auto authentication_required = decide_exit_for_response(
      make_response(UdsResponseDisposition::Rejected, std::string("authentication_required")));
    assert_equal(4,
           authentication_required.exit_code,
           "authentication challenge aliases should stay on CLI exit 4");

    const auto diag_disabled = decide_exit_for_response(
      make_response(UdsResponseDisposition::Rejected, std::string("diag_disabled")));
    assert_equal(4,
           diag_disabled.exit_code,
           "diagnostics authorization rejects should stay on CLI exit 4");
}

void test_retryable_paths_map_to_exit_code_6_and_ignore_exit_code_hint() {
  const auto not_ready = decide_exit_for_response(
      make_response(UdsResponseDisposition::NotReady, std::string("daemon_not_ready")));
  assert_equal(6,
               not_ready.exit_code,
               "not_ready responses should stay on CLI exit 6");
  assert_true(not_ready.outcome_family == CliOutcomeFamily::RetryableFailure,
              "not_ready responses should stay in retryable family");

  auto timeout = make_response(UdsResponseDisposition::Rejected,
                               std::string("runtime_dispatch_timeout"));
  timeout.exit_code_hint = 0;
  const auto timeout_decision = decide_exit_for_response(timeout);
  assert_equal(6,
               timeout_decision.exit_code,
               "retryable daemon failures should stay on CLI exit 6 even when exit_code_hint disagrees");
  assert_true(timeout_decision.diagnostic_hint.find("runtime_dispatch_timeout") !=
                  std::string::npos,
              "retryable failures should keep the stable error_ref in diagnostics");

  const auto cancel_forward_failed = decide_exit_for_response(
      make_response(UdsResponseDisposition::Rejected, std::string("cancel_forward_failed")));
  assert_equal(6,
               cancel_forward_failed.exit_code,
               "cancellation-forward failures should stay on CLI exit 6 as retryable receipt failures");
}

void test_remaining_daemon_rejects_map_to_exit_code_5() {
  const auto publish_failure = decide_exit_for_response(
      make_response(UdsResponseDisposition::Rejected, std::string("publish_encoding_failed")));
  assert_equal(5,
               publish_failure.exit_code,
               "non-retryable daemon rejects should stay on CLI exit 5");
  assert_true(publish_failure.outcome_family == CliOutcomeFamily::BusinessFailure,
              "non-retryable daemon rejects should stay in business-failure family");

  const auto receipt_expired = decide_exit_for_response(
      make_response(UdsResponseDisposition::Rejected, std::string("status_expired")));
  assert_equal(5,
               receipt_expired.exit_code,
               "expired receipt queries should stay on CLI exit 5 as non-access-deny receipt failures");
}

}  // namespace

int main() {
  try {
    test_cli_argument_failures_stay_on_exit_code_2();
    test_transport_failures_map_to_daemon_unavailable();
    test_protocol_failures_map_to_exit_code_7();
    test_success_like_paths_map_to_exit_code_0();
    test_validation_and_access_denials_keep_the_frozen_cli_families();
    test_retryable_paths_map_to_exit_code_6_and_ignore_exit_code_hint();
    test_remaining_daemon_rejects_map_to_exit_code_5();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}