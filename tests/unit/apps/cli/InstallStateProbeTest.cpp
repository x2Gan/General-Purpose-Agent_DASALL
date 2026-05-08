#include <exception>
#include <iostream>

#include "config/InstallStateProbe.h"
#include "support/TestAssertions.h"

namespace {

void test_probe_reports_fresh_install_without_canonical_files() {
  using dasall::apps::cli::config::InstallState;
  using dasall::apps::cli::config::InstallStateFacts;
  using dasall::apps::cli::config::InstallStateProbe;
  using dasall::tests::support::assert_true;

  InstallStateProbe probe;
  const auto result = probe.probe(InstallStateFacts{});
  assert_true(result.state == InstallState::FreshInstall,
              "InstallStateProbe should classify missing canonical files as FreshInstall");
}

void test_probe_reports_bootstrap_pending_for_partial_files() {
  using dasall::apps::cli::config::InstallState;
  using dasall::apps::cli::config::InstallStateFacts;
  using dasall::apps::cli::config::InstallStateProbe;
  using dasall::tests::support::assert_true;

  InstallStateProbe probe;
  InstallStateFacts facts;
  facts.defaults_file_present = true;
  const auto result = probe.probe(facts);
  assert_true(result.state == InstallState::BootstrapPending,
              "InstallStateProbe should classify partially initialized canonical files as BootstrapPending");
  assert_true(result.has_gap("profile_selection_missing"),
              "BootstrapPending should surface missing profile selection gap");
  assert_true(result.has_gap("daemon_config_missing"),
              "BootstrapPending should surface missing daemon config gap");
}

void test_probe_reports_configured_running_when_all_checks_pass() {
  using dasall::apps::cli::config::InstallState;
  using dasall::apps::cli::config::InstallStateFacts;
  using dasall::apps::cli::config::InstallStateProbe;
  using dasall::tests::support::assert_true;

  InstallStateProbe probe;
  InstallStateFacts facts;
  facts.defaults_file_present = true;
  facts.profile_id_present = true;
  facts.daemon_config_file_present = true;
  facts.daemon_config_valid = true;
  facts.secret_requirements_satisfied = true;
  facts.service_installed = true;
  facts.service_enabled = true;
  facts.service_running = true;
  facts.daemon_ping_ok = true;
  facts.daemon_readiness_ok = true;

  const auto result = probe.probe(facts);
  assert_true(result.state == InstallState::ConfiguredRunning,
              "InstallStateProbe should classify validated running deployments as ConfiguredRunning");
}

void test_probe_reports_drifted_for_invalid_config_or_readiness_mismatch() {
  using dasall::apps::cli::config::InstallState;
  using dasall::apps::cli::config::InstallStateFacts;
  using dasall::apps::cli::config::InstallStateProbe;
  using dasall::tests::support::assert_true;

  InstallStateProbe probe;
  InstallStateFacts facts;
  facts.defaults_file_present = true;
  facts.profile_id_present = true;
  facts.daemon_config_file_present = true;
  facts.daemon_config_valid = true;
  facts.secret_requirements_satisfied = true;
  facts.service_installed = true;
  facts.service_running = true;
  facts.daemon_ping_ok = true;
  facts.daemon_readiness_ok = false;

  const auto result = probe.probe(facts);
  assert_true(result.state == InstallState::Drifted,
              "InstallStateProbe should classify running-but-not-ready deployments as Drifted");
  assert_true(result.has_gap("daemon_readiness_failed"),
              "Drifted should surface readiness mismatch gaps");
}

void test_probe_reports_unsupported_without_systemd_or_payload() {
  using dasall::apps::cli::config::InstallState;
  using dasall::apps::cli::config::InstallStateFacts;
  using dasall::apps::cli::config::InstallStateProbe;
  using dasall::tests::support::assert_true;

  InstallStateProbe probe;
  InstallStateFacts facts;
  facts.systemd_available = false;
  facts.install_payload_complete = false;
  const auto result = probe.probe(facts);
  assert_true(result.state == InstallState::Unsupported,
              "InstallStateProbe should classify missing systemd/install payload as Unsupported");
  assert_true(result.has_gap("systemd_unavailable"),
              "Unsupported should surface systemd gap");
  assert_true(result.has_gap("install_payload_incomplete"),
              "Unsupported should surface install payload gap");
}

}  // namespace

int main() {
  try {
    test_probe_reports_fresh_install_without_canonical_files();
    test_probe_reports_bootstrap_pending_for_partial_files();
    test_probe_reports_configured_running_when_all_checks_pass();
    test_probe_reports_drifted_for_invalid_config_or_readiness_mismatch();
    test_probe_reports_unsupported_without_systemd_or_payload();
  } catch (const std::exception& ex) {
    std::cerr << "InstallStateProbeTest failed: " << ex.what() << '\n';
    return 1;
  }

  std::cout << "InstallStateProbeTest passed\n";
  return 0;
}