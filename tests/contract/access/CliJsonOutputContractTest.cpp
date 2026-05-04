#include <exception>
#include <iostream>
#include <string>

#include "CliExitDecision.h"
#include "CliOutputFormatter.h"
#include "support/TestAssertions.h"

namespace {

using dasall::access::daemon::UdsResponseDisposition;
using dasall::apps::cli::CliOutputMode;
using dasall::apps::cli::DaemonClientResponse;
using dasall::apps::cli::decide_exit_for_response;
using dasall::tests::support::assert_true;

[[nodiscard]] DaemonClientResponse make_response(
    const UdsResponseDisposition disposition,
    std::optional<std::string> receipt_ref = std::nullopt,
    std::optional<std::string> error_ref = std::nullopt,
    std::optional<std::string> response_text = std::nullopt,
    std::optional<bool> task_completed = std::nullopt) {
  DaemonClientResponse response;
  response.transport_ok = true;
  response.parse_ok = true;
  response.disposition = disposition;
  response.receipt_ref = std::move(receipt_ref);
  response.error_ref = std::move(error_ref);
  response.response_text = std::move(response_text);
  response.task_completed = std::move(task_completed);
  return response;
}

void test_ping_json_contract_uses_stable_cli_projection_envelope() {
  auto response = make_response(UdsResponseDisposition::Completed,
                                std::nullopt,
                                std::nullopt,
                                std::string("READY"),
                                true);
  response.request_id = "req-json-040";
  response.trace_id = "trace-json-040";

  const auto decision = decide_exit_for_response(response, CliOutputMode::Json);
  const auto json = dasall::apps::cli::CliOutputFormatter::format_json_output(
      "ping", response, decision);

  assert_true(json.find("\"schema_version\":\"cli.output.v1\"") !=
                  std::string::npos,
              "json contract must keep cli.output.v1 schema_version");
  assert_true(json.find("\"command\":\"ping\"") != std::string::npos,
              "json contract must keep canonical command name");
  assert_true(json.find("\"request_id\":\"req-json-040\"") !=
                  std::string::npos,
              "json contract must expose request_id when available");
  assert_true(json.find("\"trace_id\":\"trace-json-040\"") !=
                  std::string::npos,
              "json contract must expose trace_id when available");
  assert_true(json.find("\"disposition\":\"completed\"") !=
                  std::string::npos,
              "json contract must use completed disposition for ping success");
  assert_true(json.find("\"result\":{\"response_text\":\"READY\",\"task_completed\":true}") !=
                  std::string::npos,
              "json contract must project success payload under result");
  assert_true(json.find("\"error\":null") != std::string::npos,
              "json contract must keep top-level null error on success");
  assert_true(json.find("\"warnings\":[]") != std::string::npos,
              "json contract must always emit warnings array");
  assert_true(json.find("\"exit_code\":0") != std::string::npos,
              "json contract must expose final CLI exit code");
}

void test_accepted_async_json_contract_keeps_receipt_and_null_error() {
  auto response = make_response(UdsResponseDisposition::AcceptedAsync,
                                std::string("receipt-041"),
                                std::nullopt,
                                std::string("queued"),
                                false);

  const auto decision = decide_exit_for_response(response, CliOutputMode::Json);
  const auto json = dasall::apps::cli::CliOutputFormatter::format_json_output(
      "run", response, decision);

  assert_true(json.find("\"disposition\":\"accepted_async\"") !=
                  std::string::npos,
              "json contract must preserve accepted_async disposition");
  assert_true(json.find("\"receipt_ref\":\"receipt-041\"") !=
                  std::string::npos,
              "json contract must expose receipt_ref for accepted_async run");
  assert_true(json.find("\"task_completed\":false") != std::string::npos,
              "json contract must preserve accepted_async task_completed projection");
  assert_true(json.find("\"error\":null") != std::string::npos,
              "accepted_async contract must keep error null");
  assert_true(json.find("\"exit_code\":0") != std::string::npos,
              "accepted_async contract must stay on CLI exit code 0");
}

void test_failure_json_contract_projects_error_object_and_local_dispositions() {
  auto denied = make_response(UdsResponseDisposition::Rejected,
                              std::nullopt,
                              std::string("authorization_denied"));
  const auto denied_decision = decide_exit_for_response(denied, CliOutputMode::Json);
  const auto denied_json = dasall::apps::cli::CliOutputFormatter::format_json_output(
      "status", denied, denied_decision);

  assert_true(denied_json.find("\"disposition\":\"rejected\"") !=
                  std::string::npos,
              "failure contract must keep rejected disposition for access denials");
  assert_true(denied_json.find("\"kind\":\"access_denied\"") !=
                  std::string::npos,
              "failure contract must classify authorization denials as access_denied");
  assert_true(denied_json.find("\"error_ref\":\"authorization_denied\"") !=
                  std::string::npos,
              "failure contract must preserve daemon/access error_ref");
  assert_true(denied_json.find("\"result\":null") != std::string::npos,
              "failure contract must keep result null");
  assert_true(denied_json.find("\"exit_code\":4") != std::string::npos,
              "failure contract must keep frozen CLI access-denied exit code");

  DaemonClientResponse unavailable;
  unavailable.failure_reason = "connect timeout";
  const auto unavailable_decision = decide_exit_for_response(
      unavailable, CliOutputMode::Json);
  const auto unavailable_json =
      dasall::apps::cli::CliOutputFormatter::format_json_output(
          "ping", unavailable, unavailable_decision);

  assert_true(unavailable_json.find("\"disposition\":\"daemon_unavailable\"") !=
                  std::string::npos,
              "failure contract must project local transport failures as daemon_unavailable");
  assert_true(unavailable_json.find("\"kind\":\"transport\"") !=
                  std::string::npos,
              "failure contract must classify local transport failures as transport");
  assert_true(unavailable_json.find("\"reason\":\"connect timeout\"") !=
                  std::string::npos,
              "failure contract must preserve local failure reason");
  assert_true(unavailable_json.find("\"exit_code\":3") != std::string::npos,
              "failure contract must keep frozen daemon-unavailable exit code");
}

}  // namespace

int main() {
  try {
    test_ping_json_contract_uses_stable_cli_projection_envelope();
    test_accepted_async_json_contract_keeps_receipt_and_null_error();
    test_failure_json_contract_projects_error_object_and_local_dispositions();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}