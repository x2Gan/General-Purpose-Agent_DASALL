#include <exception>
#include <iostream>
#include <string>

#include "CliCommandParser.h"
#include "support/TestAssertions.h"

namespace {

void test_parse_run_and_submit_alias() {
  using dasall::apps::cli::CliCommandParser;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const char* run_argv[] = {"dasall_cli", "run", "{\"input\":\"hello\"}"};
  const auto run_cmd = CliCommandParser::parse(3, run_argv);
  assert_true(run_cmd.has_value(), "run command should parse with payload");
  assert_equal(std::string("run"), run_cmd->name,
               "run command should preserve canonical daemon command name");

  const char* submit_argv[] = {"dasall_cli", "submit", "{\"input\":\"hello\"}"};
  const auto submit_cmd = CliCommandParser::parse(3, submit_argv);
  assert_true(submit_cmd.has_value(),
              "submit alias should parse with payload");
  assert_equal(std::string("run"), submit_cmd->name,
               "submit alias should canonicalize to run");
}

void test_parse_status_and_cancel_arguments() {
  using dasall::apps::cli::CliCommandParser;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const char* status_argv[] = {
      "dasall_cli", "status", "receipt-031", "owner-token", "local://uid/1000"};
  const auto status_cmd = CliCommandParser::parse(5, status_argv);
  assert_true(status_cmd.has_value(), "status command should parse receipt query args");
  assert_equal(std::string("receipt-031"), *status_cmd->receipt_ref,
               "status command should capture receipt_ref");
  assert_equal(std::string("owner-token"), *status_cmd->ownership_token,
               "status command should capture ownership token");
  assert_equal(std::string("local://uid/1000"), *status_cmd->actor_ref,
               "status command should capture optional actor_ref");

  const char* cancel_argv[] = {
      "dasall_cli", "cancel", "receipt-031", "owner-token"};
  const auto cancel_cmd = CliCommandParser::parse(4, cancel_argv);
  assert_true(cancel_cmd.has_value(),
              "cancel command should parse receipt query args without actor_ref");
  assert_true(!cancel_cmd->actor_ref.has_value(),
              "cancel command should keep actor_ref optional");
}

void test_parse_diag_and_missing_status_token_reject() {
  using dasall::apps::cli::CliCommandParser;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const char* diag_argv[] = {"dasall_cli", "diag", "snapshot.get"};
  const auto diag_cmd = CliCommandParser::parse(3, diag_argv);
  assert_true(diag_cmd.has_value(), "diag command should parse hidden subcommand");
  assert_equal(std::string("diag"), diag_cmd->name,
               "diag command should preserve canonical hidden command name");
  assert_equal(std::string("snapshot.get"), *diag_cmd->diag_command,
               "diag command should capture subcommand name");

  const char* invalid_status_argv[] = {"dasall_cli", "status", "receipt-031"};
  const auto invalid_status = CliCommandParser::parse(3, invalid_status_argv);
  assert_true(!invalid_status.has_value(),
              "status command should reject missing ownership token");
}

  void test_parse_socket_path_override_and_reject_invalid_forms() {
    using dasall::apps::cli::CliCommandParser;
    using dasall::tests::support::assert_equal;
    using dasall::tests::support::assert_true;

    const char* prefixed_argv[] = {
      "dasall_cli", "--socket-path", "/tmp/dasall/override.sock", "ping"};
    const auto prefixed_cmd = CliCommandParser::parse(4, prefixed_argv);
    assert_true(prefixed_cmd.has_value(),
          "global socket-path option should parse before command name");
    assert_equal(std::string("/tmp/dasall/override.sock"),
           *prefixed_cmd->socket_path,
           "socket-path option should be preserved on parsed command");
    assert_equal(std::string("ping"), prefixed_cmd->name,
           "ping should remain the command name after extracting socket-path");

    const char* suffixed_argv[] = {
      "dasall_cli", "status", "receipt-031", "owner-token",
      "--socket-path=/tmp/dasall/override.sock"};
    const auto suffixed_cmd = CliCommandParser::parse(5, suffixed_argv);
    assert_true(suffixed_cmd.has_value(),
          "socket-path option should parse after command arguments");
    assert_equal(std::string("/tmp/dasall/override.sock"),
           *suffixed_cmd->socket_path,
           "inline socket-path option should be preserved on parsed command");

    const char* duplicate_argv[] = {
      "dasall_cli", "--socket-path", "/tmp/dasall/a.sock",
      "--socket-path=/tmp/dasall/b.sock", "ping"};
    const auto duplicate_cmd = CliCommandParser::parse(5, duplicate_argv);
    assert_true(!duplicate_cmd.has_value(),
          "duplicate socket-path options should be rejected");

    const char* missing_value_argv[] = {"dasall_cli", "--socket-path", "ping"};
    const auto missing_value_cmd = CliCommandParser::parse(3, missing_value_argv);
    assert_true(!missing_value_cmd.has_value(),
          "socket-path option without value should be rejected");

    const char* default_argv[] = {"dasall_cli", "ping"};
    const auto default_cmd = CliCommandParser::parse(2, default_argv);
    assert_true(default_cmd.has_value(),
          "ping should continue to parse without socket-path override");
    assert_true(!default_cmd->socket_path.has_value(),
          "default CLI path resolution should remain deferred to shared constant");
  }

}  // namespace

int main() {
  try {
    test_parse_run_and_submit_alias();
    test_parse_status_and_cancel_arguments();
    test_parse_diag_and_missing_status_token_reject();
    test_parse_socket_path_override_and_reject_invalid_forms();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}