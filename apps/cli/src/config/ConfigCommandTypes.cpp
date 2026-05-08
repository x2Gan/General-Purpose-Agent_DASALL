#include "config/ConfigCommandTypes.h"

namespace dasall::apps::cli::config {

namespace {

[[nodiscard]] bool string_is_present(const std::string& value) {
  return !value.empty();
}

[[nodiscard]] bool all_strings_present(
    const std::vector<std::string>& values) {
  for (const auto& value : values) {
    if (value.empty()) {
      return false;
    }
  }

  return true;
}

}  // namespace

std::string_view to_string(const InstallState state) {
  switch (state) {
    case InstallState::FreshInstall:
      return "FreshInstall";
    case InstallState::BootstrapPending:
      return "BootstrapPending";
    case InstallState::ConfiguredStopped:
      return "ConfiguredStopped";
    case InstallState::ConfiguredRunning:
      return "ConfiguredRunning";
    case InstallState::Drifted:
      return "Drifted";
    case InstallState::Unsupported:
      return "Unsupported";
  }

  return "Unsupported";
}

std::optional<InstallState> install_state_from_string(
    const std::string_view state_name) {
  if (state_name == "FreshInstall") {
    return InstallState::FreshInstall;
  }

  if (state_name == "BootstrapPending") {
    return InstallState::BootstrapPending;
  }

  if (state_name == "ConfiguredStopped") {
    return InstallState::ConfiguredStopped;
  }

  if (state_name == "ConfiguredRunning") {
    return InstallState::ConfiguredRunning;
  }

  if (state_name == "Drifted") {
    return InstallState::Drifted;
  }

  if (state_name == "Unsupported") {
    return InstallState::Unsupported;
  }

  return std::nullopt;
}

bool ConfigPlannedFileWrite::is_well_formed() const {
  return string_is_present(path) && string_is_present(operation) &&
         all_strings_present(changed_keys);
}

bool ConfigPlannedSecretWrite::is_well_formed() const {
  return string_is_present(ref) && string_is_present(operation) &&
         string_is_present(runtime_verification);
}

bool ConfigActionPlan::is_well_formed() const {
  if (schema_version != kConfigActionPlanSchemaVersion ||
      !all_strings_present(manual_followups) ||
      !all_strings_present(blocked_actions)) {
    return false;
  }

  for (const auto& file_write : file_writes) {
    if (!file_write.is_well_formed()) {
      return false;
    }
  }

  for (const auto& secret_write : secret_writes) {
    if (!secret_write.is_well_formed()) {
      return false;
    }
  }

  return true;
}

bool DesiredDaemonSettings::is_well_formed() const {
  return string_is_present(socket_path) && string_is_present(log_format);
}

bool DesiredSecretRefInput::is_well_formed() const {
  return string_is_present(ref) && string_is_present(source);
}

bool DesiredSecretSettings::is_well_formed() const {
  for (const auto& ref : refs) {
    if (!ref.is_well_formed()) {
      return false;
    }
  }

  return true;
}

bool DesiredConfigSnapshot::is_well_formed() const {
  return schema_version == kDesiredConfigSchemaVersion &&
         string_is_present(profile_id) && daemon.is_well_formed() &&
         DesiredSecretSettings(secrets).is_well_formed() &&
         all_strings_present(operator_access.add_users);
}

bool ConfigApplyResult::succeeded() const {
  return applied && !rollback_performed && blocked_actions.empty() &&
         outcome == kConfigApplyOutcomeApplied;
}

}  // namespace dasall::apps::cli::config