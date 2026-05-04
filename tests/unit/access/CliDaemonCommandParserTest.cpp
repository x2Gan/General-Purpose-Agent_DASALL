#include <exception>
#include <iostream>
#include <string>

#include "CliCommandParser.h"
#include "support/TestAssertions.h"

namespace {

void test_parse_run_and_submit_alias() {
  using dasall::apps::cli::CliCommandParser;
  using dasall::apps::cli::CliAsyncPreference;
  using dasall::apps::cli::CliOutputMode;
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

  const char* flagged_argv[] = {
      "dasall_cli",
      "--json",
      "--timeout-ms=1500",
      "run",
      "--async",
      "--request-id",
      "req-006",
      "--session",
      "session-006",
      "--trace-id=trace-006",
      "--quiet",
      "--no-input",
      "{\"input\":\"hello\"}"};
  const auto flagged_cmd = CliCommandParser::parse(13, flagged_argv);
  assert_true(flagged_cmd.has_value(),
              "run command should capture stable CLI option model fields");
  assert_true(flagged_cmd->output_mode == CliOutputMode::Json,
              "run command should capture json output mode");
  assert_true(flagged_cmd->timeout_ms.has_value() &&
                  *flagged_cmd->timeout_ms == 1500,
              "run command should capture timeout_ms value");
  assert_true(flagged_cmd->async_preference == CliAsyncPreference::Async,
              "run command should capture async preference");
  assert_equal(std::string("req-006"), *flagged_cmd->request_id,
               "run command should capture explicit request_id");
  assert_equal(std::string("session-006"), *flagged_cmd->session_hint,
               "run command should capture session hint");
  assert_equal(std::string("trace-006"), *flagged_cmd->trace_id,
               "run command should capture explicit trace_id");
  assert_true(flagged_cmd->quiet,
              "run command should capture quiet mode");
  assert_true(flagged_cmd->no_input,
              "run command should capture no-input mode");
}

void test_parse_status_and_cancel_arguments() {
  using dasall::apps::cli::CliCommandParser;
  using dasall::apps::cli::CliSelectorKind;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const char* status_argv[] = {
      "dasall_cli", "status", "receipt-031", "owner-token", "local://uid/1000"};
  const auto status_cmd = CliCommandParser::parse(5, status_argv);
  assert_true(status_cmd.has_value(), "status command should parse receipt query args");
  assert_equal(std::string("receipt-031"), *status_cmd->receipt_ref,
               "status command should capture receipt_ref");
  assert_true(status_cmd->selector_kind == CliSelectorKind::Receipt,
              "status command should classify legacy positional lookup as receipt selector");
  assert_equal(std::string("receipt-031"), *status_cmd->selector_value,
               "status command should preserve selector_value for receipt lookups");
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

  const char* invalid_scope_argv[] = {
      "dasall_cli", "status", "--async", "receipt-031", "owner-token"};
  const auto invalid_scope = CliCommandParser::parse(5, invalid_scope_argv);
  assert_true(!invalid_scope.has_value(),
              "status command should reject run-only async flag");

  const char* duplicate_request_id_argv[] = {
      "dasall_cli", "run", "--request-id=req-1", "--request-id", "req-2", "{}"};
  const auto duplicate_request_id = CliCommandParser::parse(6, duplicate_request_id_argv);
  assert_true(!duplicate_request_id.has_value(),
              "run command should reject duplicate stable option assignments");
}

void test_parse_help_and_version_commands() {
  using dasall::apps::cli::CliCommandParser;
  using dasall::apps::cli::CliOutputMode;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const char* default_help_argv[] = {"dasall_cli"};
  const auto default_help_cmd = CliCommandParser::parse(1, default_help_argv);
  assert_true(default_help_cmd.has_value(),
              "empty invocation should fall back to local help command");
  assert_equal(std::string("help"), default_help_cmd->name,
               "empty invocation should canonicalize to help");
  assert_true(default_help_cmd->help_path.empty(),
              "top-level help should not fabricate a command path");

  const char* explicit_help_argv[] = {"dasall_cli", "help", "diag", "health"};
  const auto explicit_help_cmd = CliCommandParser::parse(4, explicit_help_argv);
  assert_true(explicit_help_cmd.has_value(),
              "explicit help command should parse local help path");
  assert_equal(std::string("help"), explicit_help_cmd->name,
               "explicit help should preserve local help command name");
  assert_equal(2,
               static_cast<int>(explicit_help_cmd->help_path.size()),
               "explicit help should preserve command and subcommand path");
  assert_equal(std::string("diag"), explicit_help_cmd->help_path[0],
               "explicit help should preserve command segment");
  assert_equal(std::string("health"), explicit_help_cmd->help_path[1],
               "explicit help should preserve subcommand segment");

  const char* help_flag_argv[] = {"dasall_cli", "run", "--help"};
  const auto help_flag_cmd = CliCommandParser::parse(3, help_flag_argv);
  assert_true(help_flag_cmd.has_value(),
              "command-level --help should canonicalize to local help");
  assert_equal(std::string("help"), help_flag_cmd->name,
               "command-level --help should switch to help command");
  assert_equal(1,
               static_cast<int>(help_flag_cmd->help_path.size()),
               "command-level --help should preserve target command path");
  assert_equal(std::string("run"), help_flag_cmd->help_path[0],
               "command-level --help should target the original command");

  const char* version_argv[] = {"dasall_cli", "version", "--json", "--quiet"};
  const auto version_cmd = CliCommandParser::parse(4, version_argv);
  assert_true(version_cmd.has_value(),
              "version command should accept local-only json and quiet flags");
  assert_equal(std::string("version"), version_cmd->name,
               "version command should parse as local-only version command");
  assert_true(version_cmd->output_mode == CliOutputMode::Json,
              "version command should preserve json output mode for local dispatch");
  assert_true(version_cmd->quiet,
              "version command should preserve quiet flag for local dispatch");

  const auto version_usage = CliCommandParser::usage_string("version");
  assert_true(version_usage.find("version [--json] [--quiet]") != std::string::npos,
              "version usage should expose the frozen local-only usage skeleton");
}

void test_reject_invalid_help_and_version_flag_combinations() {
  using dasall::apps::cli::CliCommandParser;
  using dasall::tests::support::assert_true;

  const char* help_json_argv[] = {"dasall_cli", "help", "--json"};
  const auto help_json_cmd = CliCommandParser::parse(3, help_json_argv);
  assert_true(!help_json_cmd.has_value(),
              "help command should reject json mode because help is human-only");

  const char* version_socket_argv[] = {
      "dasall_cli", "version", "--socket-path", "/tmp/dasall.sock"};
  const auto version_socket_cmd = CliCommandParser::parse(4, version_socket_argv);
  assert_true(!version_socket_cmd.has_value(),
              "version command should reject daemon socket flags because it is local-only");

  const char* version_timeout_argv[] = {
      "dasall_cli", "version", "--timeout-ms", "50"};
  const auto version_timeout_cmd = CliCommandParser::parse(4, version_timeout_argv);
  assert_true(!version_timeout_cmd.has_value(),
              "version command should reject transport timeout flags because it is local-only");
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
    test_parse_help_and_version_commands();
    test_reject_invalid_help_and_version_flag_combinations();
    test_parse_socket_path_override_and_reject_invalid_forms();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}