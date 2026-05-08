#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "daemon/DaemonEndpointDefaults.h"

namespace dasall::apps::cli::config {

inline constexpr char kConfigActionPlanSchemaVersion[] =
    "dasall.config.plan.v1";
inline constexpr char kDesiredConfigSchemaVersion[] =
    "dasall.config.apply.v1";
inline constexpr char kConfigRuntimeVerificationPending[] = "pending";
inline constexpr char kConfigApplyOutcomeNotStarted[] = "not_started";
inline constexpr char kConfigApplyOutcomeApplied[] = "applied";
inline constexpr char kConfigApplyOutcomeBlocked[] = "blocked";
inline constexpr char kConfigApplyOutcomeFailedRolledBack[] =
    "apply_failed_rolled_back";

enum class InstallState {
  FreshInstall,
  BootstrapPending,
  ConfiguredStopped,
  ConfiguredRunning,
  Drifted,
  Unsupported,
};

[[nodiscard]] std::string_view to_string(InstallState state);

[[nodiscard]] std::optional<InstallState> install_state_from_string(
    std::string_view state_name);

struct ConfigPlannedFileWrite {
  std::string path;
  std::string operation;
  bool requires_root = true;
  bool restart_required = false;
  std::vector<std::string> changed_keys;

  [[nodiscard]] bool is_well_formed() const;
};

struct ConfigPlannedSecretWrite {
  std::string ref;
  std::string operation;
  std::string runtime_verification = kConfigRuntimeVerificationPending;

  [[nodiscard]] bool is_well_formed() const;
};

struct ConfigActionPlan {
  std::string schema_version = kConfigActionPlanSchemaVersion;
  InstallState state_before = InstallState::FreshInstall;
  InstallState state_after_expected = InstallState::FreshInstall;
  std::vector<ConfigPlannedFileWrite> file_writes;
  std::vector<ConfigPlannedSecretWrite> secret_writes;
  bool service_validate_requested = false;
  bool service_reload_required = false;
  bool service_restart_required = false;
  bool service_start_requested = false;
  bool service_enable_requested = false;
  std::vector<std::string> manual_followups;
  std::vector<std::string> blocked_actions;

  [[nodiscard]] bool is_well_formed() const;
};

struct DesiredDaemonSettings {
  std::string socket_path = dasall::access::daemon::kDefaultDaemonSocketPath;
  std::string log_format = "json";
  bool diag_enabled = false;
  bool override_enabled = false;
  bool watchdog_enabled = false;

  [[nodiscard]] bool is_well_formed() const;
};

struct DesiredServiceSettings {
  bool start_now = false;
  bool enable_on_boot = false;
};

struct DesiredOperatorAccessSettings {
  std::vector<std::string> add_users;
};

struct DesiredSecretRefInput {
  std::string ref;
  std::string source;

  [[nodiscard]] bool is_well_formed() const;
};

struct DesiredSecretSettings {
  std::vector<DesiredSecretRefInput> refs;

  [[nodiscard]] bool is_well_formed() const;
};

struct DesiredConfigSnapshot {
  std::string schema_version = kDesiredConfigSchemaVersion;
  std::string profile_id;
  DesiredDaemonSettings daemon;
  DesiredServiceSettings service;
  DesiredOperatorAccessSettings operator_access;
  DesiredSecretSettings secrets;

  [[nodiscard]] bool is_well_formed() const;
};

struct ConfigApplyResult {
  std::string outcome = kConfigApplyOutcomeNotStarted;
  InstallState state_before = InstallState::FreshInstall;
  InstallState state_after = InstallState::FreshInstall;
  bool applied = false;
  bool rollback_performed = false;
  std::vector<std::string> written_files;
  std::vector<std::string> written_secret_refs;
  std::vector<std::string> manual_followups;
  std::vector<std::string> blocked_actions;

  [[nodiscard]] bool succeeded() const;
};

}  // namespace dasall::apps::cli::config