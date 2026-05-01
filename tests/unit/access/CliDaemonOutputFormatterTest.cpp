#include <exception>
#include <iostream>
#include <string>

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

}  // namespace

int main() {
  try {
    test_format_ping_success_includes_disposition_and_response_text();
    test_format_submit_success_includes_receipt_for_accepted_async();
    test_format_error_includes_reason();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}