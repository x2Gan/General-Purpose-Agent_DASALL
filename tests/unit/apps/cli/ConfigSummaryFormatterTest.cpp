#include <exception>
#include <iostream>

#include "config/ConfigSummaryFormatter.h"
#include "support/TestAssertions.h"

namespace {

dasall::apps::cli::config::ConfigSummaryView make_summary_fixture() {
  using dasall::apps::cli::config::ConfigApplyResult;
  using dasall::apps::cli::config::ConfigSecretSummaryEntry;
  using dasall::apps::cli::config::ConfigSummaryView;
  using dasall::apps::cli::config::InstallState;

  ConfigSummaryView summary;
  summary.profile_id = "desktop_full";
  summary.socket_path = "/run/dasall/daemon.sock";
  summary.log_format = "json";
  summary.secret_refs.push_back(ConfigSecretSummaryEntry{
      .ref = "secret://llm/providers/deepseek-prod",
      .status = "configured",
  });
  summary.service_installed = true;
  summary.service_running = true;
  summary.service_enabled = false;
  summary.ping_status = "ready";
  summary.readiness_status = "ready";
  summary.operator_access_hint = "use sudo dasall config to modify install-state files";
  summary.incomplete_items.push_back("runtime verification pending");
  summary.next_steps.push_back("systemctl enable --now dasall-daemon.service");
  summary.apply_result = ConfigApplyResult{
      .outcome = "applied",
      .state_before = InstallState::BootstrapPending,
      .state_after = InstallState::ConfiguredRunning,
      .applied = true,
      .rollback_performed = false,
      .written_files = {"/etc/dasall/daemon.json"},
      .written_secret_refs = {"secret://llm/providers/deepseek-prod"},
      .manual_followups = {"systemctl status dasall-daemon.service"},
      .blocked_actions = {},
  };
  return summary;
}

void test_format_human_projects_profile_service_and_next_steps() {
  using dasall::apps::cli::config::ConfigSummaryFormatter;
  using dasall::tests::support::assert_true;

  const auto human = ConfigSummaryFormatter::format_human(make_summary_fixture());

  assert_true(human.find("desktop_full") != std::string::npos,
              "ConfigSummaryFormatter should include the selected profile in human output");
  assert_true(human.find("installed=true, running=true, enabled=false") !=
                  std::string::npos,
              "ConfigSummaryFormatter should summarize service state in human output");
  assert_true(human.find("secret://llm/providers/deepseek-prod (configured)") !=
                  std::string::npos,
              "ConfigSummaryFormatter should only project redacted secret refs in human output");
  assert_true(human.find("use sudo dasall config") != std::string::npos,
              "ConfigSummaryFormatter should include operator access guidance in human output");
  assert_true(human.find("systemctl enable --now dasall-daemon.service") !=
                  std::string::npos,
              "ConfigSummaryFormatter should include next-step guidance in human output");
}

void test_format_json_emits_stable_summary_schema_and_apply_result() {
  using dasall::apps::cli::config::ConfigSummaryFormatter;
  using dasall::tests::support::assert_true;

  const auto json = ConfigSummaryFormatter::format_json(make_summary_fixture());

  assert_true(json.find("\"schema_version\":\"dasall.config.summary.v1\"") !=
                  std::string::npos,
              "ConfigSummaryFormatter should emit the frozen summary schema version in JSON output");
  assert_true(json.find("\"profile_id\":\"desktop_full\"") !=
                  std::string::npos,
              "ConfigSummaryFormatter should include the selected profile in JSON output");
  assert_true(json.find("\"socket_path\":\"/run/dasall/daemon.sock\"") !=
                  std::string::npos,
              "ConfigSummaryFormatter should include daemon socket state in JSON output");
  assert_true(json.find("\"operator_access_hint\":\"use sudo dasall config to modify install-state files\"") !=
                  std::string::npos,
              "ConfigSummaryFormatter should include operator access guidance in JSON output");
  assert_true(json.find("\"outcome\":\"applied\"") != std::string::npos,
              "ConfigSummaryFormatter should embed apply_result summary in JSON output");
  assert_true(json.find("\"next_steps\":[\"systemctl enable --now dasall-daemon.service\"]") !=
                  std::string::npos,
              "ConfigSummaryFormatter should include stable next_steps arrays in JSON output");
}

}  // namespace

int main() {
  try {
    test_format_human_projects_profile_service_and_next_steps();
    test_format_json_emits_stable_summary_schema_and_apply_result();
  } catch (const std::exception& ex) {
    std::cerr << "ConfigSummaryFormatterTest failed: " << ex.what() << '\n';
    return 1;
  }

  std::cout << "ConfigSummaryFormatterTest passed\n";
  return 0;
}