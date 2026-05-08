#include <exception>
#include <iostream>

#include "config/ServiceManagerAdapter.h"
#include "support/TestAssertions.h"

namespace {

void test_plan_service_actions_maps_restart_and_enable_to_systemctl_commands() {
  using dasall::apps::cli::config::ConfigActionPlan;
  using dasall::apps::cli::config::ServiceManagerAdapter;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  ConfigActionPlan plan;
  plan.service_restart_required = true;
  plan.service_enable_requested = true;

  ServiceManagerAdapter adapter;
  const auto execution_plan = adapter.plan_service_actions(plan, true);
  assert_equal(2, static_cast<int>(execution_plan.commands.size()),
               "ServiceManagerAdapter should emit restart and enable commands when systemd is available");
  assert_true(execution_plan.commands.front().argv.size() == 3,
              "ServiceManagerAdapter should build systemctl argv triplets");
  assert_true(execution_plan.commands.front().argv[0] == "systemctl" &&
                  execution_plan.commands.front().argv[1] == "restart" &&
                  execution_plan.commands.front().argv[2] == "dasall-daemon.service",
              "ServiceManagerAdapter should restart the canonical daemon unit before enable");
  assert_true(execution_plan.commands.back().argv[1] == "enable",
              "ServiceManagerAdapter should use systemctl enable for boot persistence");
  assert_true(execution_plan.blocked_actions.empty(),
              "ServiceManagerAdapter should not block service actions when systemd is available");
}

void test_plan_service_actions_degrades_without_systemd() {
  using dasall::apps::cli::config::ConfigActionPlan;
  using dasall::apps::cli::config::ServiceManagerAdapter;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  ConfigActionPlan plan;
  plan.service_start_requested = true;
  plan.service_enable_requested = true;

  ServiceManagerAdapter adapter;
  const auto execution_plan = adapter.plan_service_actions(plan, false);
  assert_equal(0, static_cast<int>(execution_plan.commands.size()),
               "ServiceManagerAdapter should not emit systemctl commands when systemd is unavailable");
  assert_equal(2, static_cast<int>(execution_plan.blocked_actions.size()),
               "ServiceManagerAdapter should surface blocked start and enable actions without systemd");
  assert_true(!execution_plan.manual_followups.empty(),
              "ServiceManagerAdapter should provide manual followups for non-systemd environments");
}

void test_apply_stops_after_first_failed_service_action() {
  using dasall::apps::cli::config::ConfigActionPlan;
  using dasall::apps::cli::config::ServiceCommandResult;
  using dasall::apps::cli::config::ServiceManagerAdapter;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  ConfigActionPlan plan;
  plan.service_start_requested = true;
  plan.service_enable_requested = true;

  ServiceManagerAdapter adapter;
  const auto execution_plan = adapter.plan_service_actions(plan, true);
  const auto result = adapter.apply(
      execution_plan,
      [](const auto& command) {
        if (command.argv[1] == "enable") {
          return ServiceCommandResult{
              .exit_code = 5,
              .stdout_text = "",
              .stderr_text = "permission denied",
          };
        }
        return ServiceCommandResult{};
      });

  assert_true(!result.success,
              "ServiceManagerAdapter should fail apply when a service command returns non-zero");
  assert_equal(1, static_cast<int>(result.completed_actions.size()),
               "ServiceManagerAdapter should stop after the first failed action");
  assert_true(result.error_message.find("enable") != std::string::npos,
              "ServiceManagerAdapter should surface the failing action name in the error message");
}

}  // namespace

int main() {
  try {
    test_plan_service_actions_maps_restart_and_enable_to_systemctl_commands();
    test_plan_service_actions_degrades_without_systemd();
    test_apply_stops_after_first_failed_service_action();
  } catch (const std::exception& ex) {
    std::cerr << "ServiceManagerAdapterTest failed: " << ex.what() << '\n';
    return 1;
  }

  std::cout << "ServiceManagerAdapterTest passed\n";
  return 0;
}