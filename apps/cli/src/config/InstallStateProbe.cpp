#include "config/InstallStateProbe.h"

#include <string_view>

namespace dasall::apps::cli::config {

namespace {

constexpr std::string_view kGapSystemdUnavailable = "systemd_unavailable";
constexpr std::string_view kGapInstallPayloadIncomplete =
    "install_payload_incomplete";
constexpr std::string_view kGapProfileSelectionMissing =
    "profile_selection_missing";
constexpr std::string_view kGapDaemonConfigMissing = "daemon_config_missing";
constexpr std::string_view kGapDaemonConfigInvalid = "daemon_config_invalid";
constexpr std::string_view kGapSecretRequirementsMissing =
    "secret_requirements_missing";
constexpr std::string_view kGapServiceNotInstalled = "service_not_installed";
constexpr std::string_view kGapServiceNotRunning = "service_not_running";
constexpr std::string_view kGapDaemonPingFailed = "daemon_ping_failed";
constexpr std::string_view kGapDaemonReadinessFailed =
    "daemon_readiness_failed";

void add_gap_if_missing(std::vector<std::string>& gaps, std::string_view gap) {
  for (const auto& existing : gaps) {
    if (existing == gap) {
      return;
    }
  }

  gaps.emplace_back(gap);
}

}  // namespace

bool InstallStateProbeResult::has_gap(const std::string_view gap) const {
  for (const auto& existing : gaps) {
    if (existing == gap) {
      return true;
    }
  }

  return false;
}

InstallStateProbeResult InstallStateProbe::probe(
    const InstallStateFacts& facts) const {
  InstallStateProbeResult result;
  result.facts = facts;

  if (!facts.systemd_available) {
    add_gap_if_missing(result.gaps, kGapSystemdUnavailable);
  }

  if (!facts.install_payload_complete) {
    add_gap_if_missing(result.gaps, kGapInstallPayloadIncomplete);
  }

  if (!facts.systemd_available || !facts.install_payload_complete) {
    result.state = InstallState::Unsupported;
    return result;
  }

  const bool any_canonical_file_present =
      facts.defaults_file_present || facts.daemon_config_file_present;
  if (!any_canonical_file_present) {
    result.state = InstallState::FreshInstall;
    return result;
  }

  if (!facts.profile_id_present) {
    add_gap_if_missing(result.gaps, kGapProfileSelectionMissing);
  }

  if (!facts.daemon_config_file_present) {
    add_gap_if_missing(result.gaps, kGapDaemonConfigMissing);
  }

  if (!facts.defaults_file_present || !facts.profile_id_present ||
      !facts.daemon_config_file_present) {
    result.state = InstallState::BootstrapPending;
    return result;
  }

  if (!facts.daemon_config_valid) {
    add_gap_if_missing(result.gaps, kGapDaemonConfigInvalid);
  }

  if (!facts.secret_requirements_satisfied) {
    add_gap_if_missing(result.gaps, kGapSecretRequirementsMissing);
  }

  if (!facts.service_installed) {
    add_gap_if_missing(result.gaps, kGapServiceNotInstalled);
    result.state = InstallState::BootstrapPending;
    return result;
  }

  if (!facts.daemon_config_valid || !facts.secret_requirements_satisfied) {
    result.state = InstallState::Drifted;
    return result;
  }

  if (!facts.service_running) {
    add_gap_if_missing(result.gaps, kGapServiceNotRunning);
    result.state = InstallState::ConfiguredStopped;
    return result;
  }

  if (!facts.daemon_ping_ok) {
    add_gap_if_missing(result.gaps, kGapDaemonPingFailed);
  }

  if (!facts.daemon_readiness_ok) {
    add_gap_if_missing(result.gaps, kGapDaemonReadinessFailed);
  }

  if (!facts.daemon_ping_ok || !facts.daemon_readiness_ok) {
    result.state = InstallState::Drifted;
    return result;
  }

  result.state = InstallState::ConfiguredRunning;
  return result;
}

}  // namespace dasall::apps::cli::config