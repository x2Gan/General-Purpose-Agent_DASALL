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

}  // namespace

int main() {
  try {
    test_parse_run_and_submit_alias();
    test_parse_status_and_cancel_arguments();
    test_parse_diag_and_missing_status_token_reject();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}