#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "config/ConfigCommandTypes.h"
#include "support/TestAssertions.h"

namespace {

void test_install_state_round_trip() {
  using dasall::apps::cli::config::InstallState;
  using dasall::apps::cli::config::install_state_from_string;
  using dasall::apps::cli::config::to_string;
  using dasall::tests::support::assert_true;

  const std::vector<InstallState> expected_states = {
      InstallState::FreshInstall,
      InstallState::BootstrapPending,
      InstallState::ConfiguredStopped,
      InstallState::ConfiguredRunning,
      InstallState::Drifted,
      InstallState::Unsupported,
  };

  for (const auto state : expected_states) {
    const auto restored = install_state_from_string(to_string(state));
    assert_true(restored.has_value() && *restored == state,
                "InstallState should round-trip through stable string names");
  }

  assert_true(!install_state_from_string("Unknown").has_value(),
              "InstallState should reject strings outside the frozen six-state closure");
}

void test_action_plan_required_fields() {
  using dasall::apps::cli::config::ConfigActionPlan;
  using dasall::apps::cli::config::ConfigPlannedFileWrite;
  using dasall::apps::cli::config::ConfigPlannedSecretWrite;
  using dasall::apps::cli::config::InstallState;
  using dasall::tests::support::assert_true;

  ConfigActionPlan plan;
  plan.state_before = InstallState::BootstrapPending;
  plan.state_after_expected = InstallState::ConfiguredRunning;
  plan.file_writes.push_back(ConfigPlannedFileWrite{
      .path = "/etc/dasall/daemon.json",
      .operation = "update",
      .requires_root = true,
      .restart_required = true,
      .changed_keys = {"daemon.socket_path", "daemon.log_format"},
  });
  plan.secret_writes.push_back(ConfigPlannedSecretWrite{
      .ref = "secret://llm/providers/deepseek-prod",
      .operation = "create_or_rotate",
      .runtime_verification = "pending",
  });
  plan.service_validate_requested = true;
  plan.service_restart_required = true;
  plan.service_start_requested = true;

  assert_true(plan.is_well_formed(),
              "ConfigActionPlan should accept the frozen top-level schema with well-formed entries");

  plan.file_writes[0].path.clear();
  assert_true(!plan.is_well_formed(),
              "ConfigActionPlan should reject file write entries that omit required path fields");
}

void test_desired_config_snapshot_defaults() {
  using dasall::access::daemon::kDefaultDaemonSocketPath;
  using dasall::apps::cli::config::DesiredConfigSnapshot;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  DesiredConfigSnapshot snapshot;
  assert_equal(std::string("dasall.config.apply.v1"), snapshot.schema_version,
               "DesiredConfigSnapshot should default to the frozen v1 schema version");
  assert_equal(std::string(kDefaultDaemonSocketPath), snapshot.daemon.socket_path,
               "DesiredConfigSnapshot should reuse the shared daemon socket default");
  assert_equal(std::string("json"), snapshot.daemon.log_format,
               "DesiredConfigSnapshot should default daemon log_format to json");
  assert_true(!snapshot.is_well_formed(),
              "DesiredConfigSnapshot should reject missing required profile_id before apply planning");

  snapshot.profile_id = "desktop_full";
  snapshot.operator_access.add_users.push_back("root");
  snapshot.secrets.refs.push_back({
      .ref = "secret://llm/providers/deepseek-prod",
      .source = "stdin",
  });
  assert_true(snapshot.is_well_formed(),
              "DesiredConfigSnapshot should accept the minimal frozen apply model once required fields are present");

  snapshot.secrets.refs[0].source.clear();
  assert_true(!snapshot.is_well_formed(),
              "DesiredConfigSnapshot should reject malformed secret refs with missing source metadata");
}

void test_apply_result_success_projection() {
  using dasall::apps::cli::config::ConfigApplyResult;
  using dasall::apps::cli::config::InstallState;
  using dasall::tests::support::assert_true;

  ConfigApplyResult result;
  assert_true(!result.succeeded(),
              "ConfigApplyResult should default to not started and not report success");

  result.outcome = "applied";
  result.state_before = InstallState::BootstrapPending;
  result.state_after = InstallState::ConfiguredRunning;
  result.applied = true;
  result.written_files.push_back("/etc/dasall/daemon.json");
  assert_true(result.succeeded(),
              "ConfigApplyResult should project success only when apply completed without rollback or blocks");

  result.blocked_actions.push_back("systemd unavailable");
  assert_true(!result.succeeded(),
              "ConfigApplyResult should not project success when blocked actions remain");
}

}  // namespace

int main() {
  try {
    test_install_state_round_trip();
    test_action_plan_required_fields();
    test_desired_config_snapshot_defaults();
    test_apply_result_success_projection();
  } catch (const std::exception& ex) {
    std::cerr << "ConfigCommandTypesTest failed: " << ex.what() << '\n';
    return 1;
  }

  std::cout << "ConfigCommandTypesTest passed\n";
  return 0;
}