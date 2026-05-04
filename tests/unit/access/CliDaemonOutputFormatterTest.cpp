#include <exception>
#include <iostream>
#include <string>

#include "CliExitDecision.h"
#include "CliIpcClient.h"
#include "CliOutputFormatter.h"
#include "support/TestAssertions.h"

namespace {

dasall::apps::cli::DaemonClientResponse make_response(
    const dasall::access::daemon::UdsResponseDisposition disposition,
    std::optional<std::string> receipt_ref,
    std::optional<std::string> error_ref,
    std::optional<std::string> response_text) {
  dasall::apps::cli::DaemonClientResponse response;
  response.transport_ok = true;
  response.parse_ok = true;
  response.disposition = disposition;
  response.receipt_ref = std::move(receipt_ref);
  response.error_ref = std::move(error_ref);
  response.response_text = std::move(response_text);
  return response;
}

void test_format_ping_success_includes_disposition_and_response_text() {
  using dasall::access::daemon::UdsResponseDisposition;
  using dasall::apps::cli::CliOutputFormatter;
  using dasall::tests::support::assert_true;

  const auto response = make_response(UdsResponseDisposition::Completed,
                                      std::nullopt,
                                      std::nullopt,
                                      std::string("READY"));
  const auto message = CliOutputFormatter::format_ping_success(response);

  assert_true(message.find("completed") != std::string::npos,
              "ping formatter should include completed disposition");
  assert_true(message.find("READY") != std::string::npos,
              "ping formatter should include readiness summary text");
}

void test_format_submit_success_includes_receipt_for_accepted_async() {
  using dasall::access::daemon::UdsResponseDisposition;
  using dasall::apps::cli::CliOutputFormatter;
  using dasall::tests::support::assert_true;

  const auto response = make_response(UdsResponseDisposition::AcceptedAsync,
                                      std::string("receipt-031"),
                                      std::nullopt,
                                      std::string("queued"));
  const auto message = CliOutputFormatter::format_submit_success(response);

  assert_true(message.find("accepted_async") != std::string::npos,
              "submit formatter should include accepted_async disposition");
  assert_true(message.find("receipt-031") != std::string::npos,
              "submit formatter should include receipt reference");
}

void test_format_error_includes_reason() {
  using dasall::apps::cli::CliOutputFormatter;
  using dasall::tests::support::assert_true;

  const auto message = CliOutputFormatter::format_error("authorization_denied");
  assert_true(message.find("authorization_denied") != std::string::npos,
              "error formatter should include explicit failure reason");
}

  void test_format_json_output_emits_stable_completed_envelope() {
    using dasall::access::daemon::UdsResponseDisposition;
    using dasall::apps::cli::CliExitDecision;
    using dasall::apps::cli::CliOutcomeFamily;
    using dasall::apps::cli::CliOutputFormatter;
    using dasall::apps::cli::CliPrimaryOutputStream;
    using dasall::tests::support::assert_true;

    auto response = make_response(UdsResponseDisposition::Completed,
                  std::nullopt,
                  std::nullopt,
                  std::string("READY"));
    response.request_id = "req-040";
    response.trace_id = "trace-040";
    response.task_completed = true;

    const CliExitDecision decision{
      .exit_code = 0,
      .primary_output_stream = CliPrimaryOutputStream::Stdout,
      .json_mode = true,
      .outcome_family = CliOutcomeFamily::Success,
      .diagnostic_hint = {},
    };

    const auto json =
      CliOutputFormatter::format_json_output("ping", response, decision);

    assert_true(json.find("\"schema_version\":\"cli.output.v1\"") !=
            std::string::npos,
          "json formatter should emit stable schema version");
    assert_true(json.find("\"command\":\"ping\"") != std::string::npos,
          "json formatter should emit canonical command name");
    assert_true(json.find("\"request_id\":\"req-040\"") !=
            std::string::npos,
          "json formatter should emit request_id when present");
    assert_true(json.find("\"disposition\":\"completed\"") !=
            std::string::npos,
          "json formatter should emit completed disposition for success");
    assert_true(json.find("\"response_text\":\"READY\"") !=
            std::string::npos,
          "json formatter should project response_text into result object");
    assert_true(json.find("\"error\":null") != std::string::npos,
          "json formatter should emit null error on success");
    assert_true(json.find("\"warnings\":[]") != std::string::npos,
          "json formatter should always emit warnings array");
    assert_true(json.find("\"exit_code\":0") != std::string::npos,
          "json formatter should emit final CLI exit code");
  }

  void test_format_json_output_projects_transport_failure() {
    using dasall::apps::cli::CliExitDecision;
    using dasall::apps::cli::CliOutcomeFamily;
    using dasall::apps::cli::CliOutputFormatter;
    using dasall::apps::cli::CliPrimaryOutputStream;
    using dasall::tests::support::assert_true;

    dasall::apps::cli::DaemonClientResponse response;
    response.failure_reason = "connect timeout";

    const CliExitDecision decision{
      .exit_code = 3,
      .primary_output_stream = CliPrimaryOutputStream::Stderr,
      .json_mode = true,
      .outcome_family = CliOutcomeFamily::DaemonUnavailable,
      .diagnostic_hint = "connect timeout",
    };

    const auto json =
      CliOutputFormatter::format_json_output("ping", response, decision);

    assert_true(json.find("\"disposition\":\"daemon_unavailable\"") !=
            std::string::npos,
          "json formatter should project local transport failures to daemon_unavailable");
    assert_true(json.find("\"kind\":\"transport\"") != std::string::npos,
          "json formatter should emit transport error kind for daemon-unavailable failures");
    assert_true(json.find("\"reason\":\"connect timeout\"") !=
            std::string::npos,
          "json formatter should expose diagnostic reason for transport failures");
    assert_true(json.find("\"result\":null") != std::string::npos,
          "json formatter should omit result object on failures");
    assert_true(json.find("\"exit_code\":3") != std::string::npos,
          "json formatter should preserve daemon-unavailable exit code");
  }

}  // namespace

int main() {
  try {
    test_format_ping_success_includes_disposition_and_response_text();
    test_format_submit_success_includes_receipt_for_accepted_async();
    test_format_error_includes_reason();
    test_format_json_output_emits_stable_completed_envelope();
    test_format_json_output_projects_transport_failure();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}