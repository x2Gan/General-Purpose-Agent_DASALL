#include <exception>
#include <iostream>

#include "config/ConfigPreflightChecker.h"
#include "support/TestAssertions.h"

namespace {

[[nodiscard]] dasall::apps::cli::config::DesiredConfigSnapshot make_desired() {
  dasall::apps::cli::config::DesiredConfigSnapshot desired;
  desired.profile_id = "desktop_full";
  return desired;
}

void test_run_blocks_non_root_write_requests() {
  using dasall::apps::cli::config::ConfigActionPlan;
  using dasall::apps::cli::config::ConfigPlannedFileWrite;
  using dasall::apps::cli::config::ConfigPreflightChecker;
  using dasall::apps::cli::config::ConfigPreflightEnvironment;
  using dasall::apps::cli::config::PrivilegeContext;
  using dasall::tests::support::assert_true;

  ConfigActionPlan plan;
  plan.file_writes.push_back(ConfigPlannedFileWrite{
      .path = "/etc/dasall/daemon.json",
      .operation = "update",
      .requires_root = true,
      .changed_keys = {},
  });

  ConfigPreflightEnvironment environment;
  environment.daemon_binary_available = true;
  environment.defaults_file_writable = true;
  environment.daemon_config_file_writable = true;
  ConfigPreflightChecker checker(environment);

  const auto result = checker.run(
      plan,
      make_desired(),
      [](const auto&) { return dasall::apps::cli::config::ValidateOnlyResult{}; },
      PrivilegeContext{.running_as_root = false, .stdin_is_tty = true});

  assert_true(!result.ok,
              "ConfigPreflightChecker should reject write plans when the operator is not root");
  assert_true(result.root_required,
              "ConfigPreflightChecker should surface root requirement for canonical file writes");
  assert_true(result.has_reason("root_required_for_config_write"),
              "ConfigPreflightChecker should emit a stable non-root failure reason");
}

void test_run_accepts_successful_validate_only_preflight() {
  using dasall::apps::cli::config::ConfigActionPlan;
  using dasall::apps::cli::config::ConfigPreflightChecker;
  using dasall::apps::cli::config::ConfigPreflightEnvironment;
  using dasall::apps::cli::config::PrivilegeContext;
  using dasall::tests::support::assert_true;

  ConfigActionPlan plan;
  plan.service_validate_requested = true;

  ConfigPreflightEnvironment environment;
  environment.daemon_binary_available = true;
  environment.defaults_file_writable = true;
  environment.daemon_config_file_writable = true;
  ConfigPreflightChecker checker(environment);

  bool invoked = false;
  const auto result = checker.run(
      plan,
      make_desired(),
      [&invoked](const auto& command) {
        invoked = true;
        dasall::tests::support::assert_true(
            command.size() >= 5 && command[1] == "--validate-only" &&
                command[2] == "--config-file",
            "ConfigPreflightChecker should invoke dasall-daemon validate-only with canonical config path arguments");
        return dasall::apps::cli::config::ValidateOnlyResult{
            .exit_code = 0,
            .stdout_text = "config validation passed without creating listener resources",
          .stderr_text = "",
        };
      },
      PrivilegeContext{.running_as_root = true, .stdin_is_tty = true});

  assert_true(invoked,
              "ConfigPreflightChecker should run validate-only when the action plan requests it");
  assert_true(result.ok,
              "ConfigPreflightChecker should pass when validate-only succeeds and no other preflight blocker exists");
  assert_true(result.validate_only_passed,
              "ConfigPreflightChecker should surface validate-only success in the result object");
}

void test_run_reports_validate_only_failure() {
  using dasall::apps::cli::config::ConfigActionPlan;
  using dasall::apps::cli::config::ConfigPreflightChecker;
  using dasall::apps::cli::config::ConfigPreflightEnvironment;
  using dasall::apps::cli::config::PrivilegeContext;
  using dasall::tests::support::assert_true;

  ConfigActionPlan plan;
  plan.service_validate_requested = true;

  ConfigPreflightEnvironment environment;
  environment.daemon_binary_available = true;
  environment.defaults_file_writable = true;
  environment.daemon_config_file_writable = true;
  ConfigPreflightChecker checker(environment);

  const auto result = checker.run(
      plan,
      make_desired(),
      [](const auto&) {
        return dasall::apps::cli::config::ValidateOnlyResult{
            .exit_code = 1,
          .stdout_text = "",
            .stderr_text = "config validation failed",
        };
      },
      PrivilegeContext{.running_as_root = true, .stdin_is_tty = true});

  assert_true(!result.ok,
              "ConfigPreflightChecker should fail the preflight when validate-only returns non-zero");
  assert_true(!result.validate_only_passed,
              "ConfigPreflightChecker should mark validate-only as failed when the daemon rejects the config");
  assert_true(result.has_reason("daemon_validate_only_failed"),
              "ConfigPreflightChecker should emit a stable validate-only failure reason");
}

}  // namespace

int main() {
  try {
    test_run_blocks_non_root_write_requests();
    test_run_accepts_successful_validate_only_preflight();
    test_run_reports_validate_only_failure();
  } catch (const std::exception& ex) {
    std::cerr << "ConfigPreflightCheckerTest failed: " << ex.what() << '\n';
    return 1;
  }

  std::cout << "ConfigPreflightCheckerTest passed\n";
  return 0;
}