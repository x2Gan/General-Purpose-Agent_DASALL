#include <exception>
#include <iostream>
#include <string>

#include "CliCommandParser.h"
#include "support/TestAssertions.h"

namespace {

void test_parse_config_subcommands() {
  using dasall::apps::cli::CliCommandParser;
  using dasall::apps::cli::CliConfigCommandKind;
  using dasall::apps::cli::CliOutputMode;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const char* wizard_argv[] = {"dasall-cli", "config"};
  const auto wizard_cmd = CliCommandParser::parse(2, wizard_argv);
  assert_true(wizard_cmd.has_value(),
              "config without subcommand should parse as interactive wizard");
  assert_equal(std::string("config"), wizard_cmd->name,
               "config wizard should preserve local command family name");
  assert_true(wizard_cmd->config_command == CliConfigCommandKind::Wizard,
              "config without subcommand should map to wizard kind");

  const char* show_argv[] = {"dasall-cli", "config", "show", "--json"};
  const auto show_cmd = CliCommandParser::parse(4, show_argv);
  assert_true(show_cmd.has_value(),
              "config show should parse as local non-interactive command");
  assert_true(show_cmd->config_command == CliConfigCommandKind::Show,
              "config show should map to show kind");
  assert_true(show_cmd->output_mode == CliOutputMode::Json,
              "config show should preserve json output mode");

  const char* plan_argv[] = {"dasall-cli", "config", "plan", "--from-file",
                             "/tmp/dasall-config.yaml", "--dry-run", "--json"};
  const auto plan_cmd = CliCommandParser::parse(7, plan_argv);
  assert_true(plan_cmd.has_value(),
              "config plan should accept optional from-file and dry-run flags");
  assert_true(plan_cmd->config_command == CliConfigCommandKind::Plan,
              "config plan should map to plan kind");
  assert_equal(std::string("/tmp/dasall-config.yaml"),
               *plan_cmd->config_from_file,
               "config plan should preserve from-file path");
  assert_true(plan_cmd->config_dry_run,
              "config plan should capture dry-run compatibility flag");

  const char* validate_argv[] = {"dasall-cli", "config", "validate"};
  const auto validate_cmd = CliCommandParser::parse(3, validate_argv);
  assert_true(validate_cmd.has_value(),
              "config validate should parse as local validation command");
  assert_true(validate_cmd->config_command == CliConfigCommandKind::Validate,
              "config validate should map to validate kind");

  const char* apply_argv[] = {"dasall-cli", "config", "apply", "--from-file",
                              "/tmp/dasall-config.yaml", "--no-input", "--json"};
  const auto apply_cmd = CliCommandParser::parse(7, apply_argv);
  assert_true(apply_cmd.has_value(),
              "config apply should require headless from-file and no-input grammar");
  assert_true(apply_cmd->config_command == CliConfigCommandKind::Apply,
              "config apply should map to apply kind");
  assert_equal(std::string("/tmp/dasall-config.yaml"),
               *apply_cmd->config_from_file,
               "config apply should preserve from-file path");
  assert_true(apply_cmd->no_input,
              "config apply should preserve no-input guard");
}

void test_reject_invalid_config_flag_combinations() {
  using dasall::apps::cli::CliCommandParser;
  using dasall::tests::support::assert_true;

  const char* config_json_argv[] = {"dasall-cli", "config", "--json"};
  const auto config_json_cmd = CliCommandParser::parse(3, config_json_argv);
  assert_true(!config_json_cmd.has_value(),
              "interactive config should reject json mode because wizard remains human-only");

  const char* show_dry_run_argv[] = {"dasall-cli", "config", "show", "--dry-run"};
  const auto show_dry_run_cmd = CliCommandParser::parse(4, show_dry_run_argv);
  assert_true(!show_dry_run_cmd.has_value(),
              "config show should reject plan-only dry-run flag");

  const char* validate_from_file_argv[] = {"dasall-cli", "config", "validate",
                                           "--from-file", "/tmp/dasall.yaml"};
  const auto validate_from_file_cmd =
      CliCommandParser::parse(5, validate_from_file_argv);
  assert_true(!validate_from_file_cmd.has_value(),
              "config validate should reject apply/plan-only from-file flag");

  const char* apply_missing_file_argv[] = {"dasall-cli", "config", "apply",
                                           "--no-input"};
  const auto apply_missing_file_cmd =
      CliCommandParser::parse(4, apply_missing_file_argv);
  assert_true(!apply_missing_file_cmd.has_value(),
              "config apply should reject missing from-file path");

  const char* apply_missing_no_input_argv[] = {"dasall-cli", "config", "apply",
                                               "--from-file", "/tmp/dasall.yaml"};
  const auto apply_missing_no_input_cmd =
      CliCommandParser::parse(5, apply_missing_no_input_argv);
  assert_true(!apply_missing_no_input_cmd.has_value(),
              "config apply should reject missing no-input confirmation guard");

  const char* config_socket_argv[] = {"dasall-cli", "config", "show",
                                      "--socket-path", "/tmp/dasall.sock"};
  const auto config_socket_cmd = CliCommandParser::parse(5, config_socket_argv);
  assert_true(!config_socket_cmd.has_value(),
              "config commands should reject daemon transport flags because they are local-only");
}

void test_config_help_usage() {
  using dasall::apps::cli::CliCommandParser;
  using dasall::tests::support::assert_true;

  const auto config_usage = CliCommandParser::usage_string("config");
  assert_true(config_usage.find("dasall-cli config apply --from-file <path> --no-input [--json]") !=
                  std::string::npos,
              "config usage should list the full v1 command family");

  const auto config_plan_usage = CliCommandParser::usage_string("config", "plan");
  assert_true(config_plan_usage.find("config plan [--from-file <path>] [--dry-run] [--json]") !=
                  std::string::npos,
              "config plan help should expose the frozen headless grammar");
}

}  // namespace

int main() {
  try {
    test_parse_config_subcommands();
    test_reject_invalid_config_flag_combinations();
    test_config_help_usage();
  } catch (const std::exception& ex) {
    std::cerr << "ConfigCommandParserTest failed: " << ex.what() << '\n';
    return 1;
  }

  std::cout << "ConfigCommandParserTest passed\n";
  return 0;
}