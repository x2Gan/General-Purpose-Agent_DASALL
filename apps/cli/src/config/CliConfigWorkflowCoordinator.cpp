#include "config/CliConfigWorkflowCoordinator.h"

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string_view>
#include <thread>
#include <utility>

#include "daemon/DaemonEndpointDefaults.h"

#include "config/ConfigPlanFormatter.h"

namespace dasall::apps::cli::config {

namespace {

namespace fs = std::filesystem;

constexpr std::string_view kOperatorAccessHint =
  "use sudo dasall config to modify install-state files";
constexpr std::string_view kSummaryNextStepFreshInstall =
  "run 'dasall-cli config' in a TTY to initialize canonical deployment files";
constexpr std::string_view kSummaryNextStepBootstrapPending =
  "complete the missing canonical config inputs before applying service actions";
constexpr std::string_view kSummaryNextStepDrifted =
  "run 'dasall-cli config validate' to inspect deterministic config failures";
constexpr std::string_view kSummaryNextStepConfiguredStopped =
  "review the planned service actions before starting dasall-daemon";
constexpr std::string_view kSummaryNextStepConfiguredRunning =
  "review 'dasall-cli config plan' before changing canonical deployment files";
constexpr std::string_view kPlanFromFileNotReady =
  "config plan --from-file requires the diff planner from CLCFG-TODO-013";
constexpr std::string_view kValidateProfileMissing =
  "config validate requires a canonical profile selection in /etc/default/dasall-daemon";
constexpr std::string_view kValidateDaemonConfigMissing =
  "config validate requires a canonical daemon.json before validate-only can run";
constexpr std::string_view kApplyFromFileMissing =
  "config apply requires --from-file <path> to specify the desired-state document";
constexpr std::string_view kApplySecretWritesBlocked =
  "config apply cannot materialize secret writes until CLCFG-TODO-016 lands";
constexpr std::string_view kWizardNoInputNotAllowed =
  "interactive config does not support --no-input; use config apply --from-file <path> --no-input";
constexpr std::string_view kWizardApplyCancelled =
  "config wizard apply cancelled by operator";

struct ObservedConfigState {
  DaemonConfigStoreSnapshot snapshot;
  DesiredConfigSnapshot desired;
  InstallStateFacts facts;
  InstallStateProbeResult probe_result;
};

[[nodiscard]] std::string_view workflow_command_name(
    const dasall::apps::cli::CliConfigCommandKind command_kind) {
  using dasall::apps::cli::CliConfigCommandKind;

  switch (command_kind) {
    case CliConfigCommandKind::Wizard:
      return "config";
    case CliConfigCommandKind::Show:
      return "config.show";
    case CliConfigCommandKind::Plan:
      return "config.plan";
    case CliConfigCommandKind::Validate:
      return "config.validate";
    case CliConfigCommandKind::Apply:
      return "config.apply";
    case CliConfigCommandKind::None:
      return "config";
  }

  return "config";
}

[[nodiscard]] std::string pending_reason(
    const dasall::apps::cli::CliConfigCommandKind command_kind) {
  using dasall::apps::cli::CliConfigCommandKind;

  switch (command_kind) {
    case CliConfigCommandKind::Wizard:
      return "interactive config workflow skeleton is ready; desired-state collection is pending implementation";
    case CliConfigCommandKind::Show:
      return "config show workflow skeleton is ready; summary projection is pending implementation";
    case CliConfigCommandKind::Plan:
      return "config plan workflow skeleton is ready; diff planning is pending implementation";
    case CliConfigCommandKind::Validate:
      return "config validate workflow skeleton is ready; validate-only execution is pending implementation";
    case CliConfigCommandKind::Apply:
      return "config apply workflow skeleton is ready; apply executor is pending implementation";
    case CliConfigCommandKind::None:
      return "config workflow skeleton is ready; command dispatch is pending implementation";
  }

  return "config workflow skeleton is ready; command dispatch is pending implementation";
}

[[nodiscard]] std::string render_for_output_mode(
    const dasall::apps::cli::CliCommand& command,
    const std::string& human_output,
    const std::string& json_output) {
  return command.output_mode == dasall::apps::cli::CliOutputMode::Json
             ? json_output
             : human_output;
}

void add_unique_string(std::vector<std::string>& values,
                       std::string_view value) {
  for (const auto& existing : values) {
    if (existing == value) {
      return;
    }
  }

  values.emplace_back(value);
}

[[nodiscard]] bool contains_string(const std::vector<std::string>& values,
                                   std::string_view expected) {
  for (const auto& value : values) {
    if (value == expected) {
      return true;
    }
  }

  return false;
}

[[nodiscard]] std::string bool_text(const bool value) {
  return value ? "true" : "false";
}

[[nodiscard]] std::string escape_json_string(std::string_view input) {
  std::string output;
  output.reserve(input.size());
  for (const unsigned char current : input) {
    switch (current) {
      case '\\':
        output += "\\\\";
        break;
      case '"':
        output += "\\\"";
        break;
      case '\n':
        output += "\\n";
        break;
      case '\r':
        output += "\\r";
        break;
      case '\t':
        output += "\\t";
        break;
      default:
        output.push_back(static_cast<char>(current));
        break;
    }
  }
  return output;
}

[[nodiscard]] std::string json_string(std::string_view input) {
  return std::string("\"") + escape_json_string(input) + "\"";
}

[[nodiscard]] std::string json_string_array(
    const std::vector<std::string>& values) {
  std::string output = "[";
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index != 0) {
      output += ',';
    }
    output += json_string(values[index]);
  }
  output += ']';
  return output;
}

void skip_json_whitespace(std::string_view json, std::size_t& index) {
  while (index < json.size() &&
         std::isspace(static_cast<unsigned char>(json[index])) != 0) {
    ++index;
  }
}

[[nodiscard]] std::optional<std::string> extract_json_string_value(
    std::string_view json,
    std::string_view key) {
  const std::string needle = "\"" + std::string(key) + "\":";
  const std::size_t key_pos = json.find(needle);
  if (key_pos == std::string_view::npos) {
    return std::nullopt;
  }

  std::size_t value_pos = key_pos + needle.size();
  skip_json_whitespace(json, value_pos);
  if (value_pos >= json.size() || json[value_pos] != '"') {
    return std::nullopt;
  }

  ++value_pos;
  std::string value;
  bool escaped = false;
  while (value_pos < json.size()) {
    const char current = json[value_pos++];
    if (escaped) {
      value.push_back(current);
      escaped = false;
      continue;
    }
    if (current == '\\') {
      escaped = true;
      continue;
    }
    if (current == '"') {
      return value;
    }
    value.push_back(current);
  }

  return std::nullopt;
}

[[nodiscard]] std::optional<bool> extract_json_bool_value(
    std::string_view json,
    std::string_view key) {
  const std::string needle = "\"" + std::string(key) + "\":";
  const std::size_t key_pos = json.find(needle);
  if (key_pos == std::string_view::npos) {
    return std::nullopt;
  }

  std::size_t value_pos = key_pos + needle.size();
  skip_json_whitespace(json, value_pos);
  if (json.substr(value_pos, 4) == "true") {
    return true;
  }
  if (json.substr(value_pos, 5) == "false") {
    return false;
  }
  return std::nullopt;
}

[[nodiscard]] std::string read_all_from_fd(const int fd) {
  std::string output;
  char buffer[4096];
  ssize_t read_count = 0;
  while ((read_count = ::read(fd, buffer, sizeof(buffer))) > 0) {
    output.append(buffer, static_cast<std::size_t>(read_count));
  }
  return output;
}

[[nodiscard]] ValidateOnlyResult default_validate_only_runner(
    const std::vector<std::string>& command) {
  ValidateOnlyResult result;
  if (command.empty()) {
    result.exit_code = 127;
    result.stderr_text = "validate-only command is empty";
    return result;
  }

  int stdout_pipe[2];
  int stderr_pipe[2];
  if (::pipe(stdout_pipe) != 0 || ::pipe(stderr_pipe) != 0) {
    result.exit_code = 127;
    result.stderr_text = "failed to create validate-only pipes";
    return result;
  }

  const pid_t pid = ::fork();
  if (pid < 0) {
    ::close(stdout_pipe[0]);
    ::close(stdout_pipe[1]);
    ::close(stderr_pipe[0]);
    ::close(stderr_pipe[1]);
    result.exit_code = 127;
    result.stderr_text = "failed to fork validate-only child";
    return result;
  }

  if (pid == 0) {
    ::dup2(stdout_pipe[1], STDOUT_FILENO);
    ::dup2(stderr_pipe[1], STDERR_FILENO);
    ::close(stdout_pipe[0]);
    ::close(stdout_pipe[1]);
    ::close(stderr_pipe[0]);
    ::close(stderr_pipe[1]);

    std::vector<char*> argv;
    argv.reserve(command.size() + 1U);
    for (const auto& arg : command) {
      argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);

    ::execv(argv.front(), argv.data());
    std::perror("execv");
    std::_Exit(127);
  }

  ::close(stdout_pipe[1]);
  ::close(stderr_pipe[1]);

  std::thread stdout_reader([&result, fd = stdout_pipe[0]]() {
    result.stdout_text = read_all_from_fd(fd);
    ::close(fd);
  });
  std::thread stderr_reader([&result, fd = stderr_pipe[0]]() {
    result.stderr_text = read_all_from_fd(fd);
    ::close(fd);
  });

  int status = 0;
  if (::waitpid(pid, &status, 0) < 0) {
    result.exit_code = 127;
    result.stderr_text = "failed to wait for validate-only child";
  } else if (WIFEXITED(status)) {
    result.exit_code = WEXITSTATUS(status);
  } else if (WIFSIGNALED(status)) {
    result.exit_code = 128 + WTERMSIG(status);
  }

  stdout_reader.join();
  stderr_reader.join();
  return result;
}

[[nodiscard]] ServiceCommandResult default_service_command_runner(
    const ServiceManagerCommand& command) {
  ServiceCommandResult result;
  if (command.argv.empty()) {
    result.exit_code = 127;
    result.stderr_text = "service command is empty";
    return result;
  }

  int stdout_pipe[2];
  int stderr_pipe[2];
  if (::pipe(stdout_pipe) != 0 || ::pipe(stderr_pipe) != 0) {
    result.exit_code = 127;
    result.stderr_text = "failed to create service command pipes";
    return result;
  }

  const pid_t pid = ::fork();
  if (pid < 0) {
    ::close(stdout_pipe[0]);
    ::close(stdout_pipe[1]);
    ::close(stderr_pipe[0]);
    ::close(stderr_pipe[1]);
    result.exit_code = 127;
    result.stderr_text = "failed to fork service command child";
    return result;
  }

  if (pid == 0) {
    ::dup2(stdout_pipe[1], STDOUT_FILENO);
    ::dup2(stderr_pipe[1], STDERR_FILENO);
    ::close(stdout_pipe[0]);
    ::close(stdout_pipe[1]);
    ::close(stderr_pipe[0]);
    ::close(stderr_pipe[1]);

    std::vector<char*> argv;
    argv.reserve(command.argv.size() + 1U);
    for (const auto& arg : command.argv) {
      argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);

    ::execvp(argv.front(), argv.data());
    std::perror("execvp");
    std::_Exit(127);
  }

  ::close(stdout_pipe[1]);
  ::close(stderr_pipe[1]);

  std::thread stdout_reader([&result, fd = stdout_pipe[0]]() {
    result.stdout_text = read_all_from_fd(fd);
    ::close(fd);
  });
  std::thread stderr_reader([&result, fd = stderr_pipe[0]]() {
    result.stderr_text = read_all_from_fd(fd);
    ::close(fd);
  });

  int status = 0;
  if (::waitpid(pid, &status, 0) < 0) {
    result.exit_code = 127;
    result.stderr_text = "failed to wait for service command child";
  } else if (WIFEXITED(status)) {
    result.exit_code = WEXITSTATUS(status);
  } else if (WIFSIGNALED(status)) {
    result.exit_code = 128 + WTERMSIG(status);
  }

  stdout_reader.join();
  stderr_reader.join();
  return result;
}

[[nodiscard]] InstallStateFacts build_install_state_facts(
    const DaemonConfigStoreSnapshot& snapshot,
    const ConfigPreflightEnvironment& environment) {
  InstallStateFacts facts;
  facts.systemd_available =
      environment.systemd_available.value_or(fs::exists("/run/systemd/system"));

  const bool daemon_binary_available = environment.daemon_binary_available.value_or(
      ::access(environment.daemon_binary.c_str(), X_OK) == 0);
  facts.install_payload_complete = daemon_binary_available;
  facts.defaults_file_present = snapshot.defaults_file_exists;
  facts.profile_id_present = snapshot.profile_id.has_value();
  facts.daemon_config_file_present = snapshot.daemon_config_file_exists;
  facts.daemon_config_valid = !snapshot.daemon_config_file_exists ||
                              snapshot.daemon_config_valid;
  facts.secret_requirements_satisfied = true;
  facts.service_installed = daemon_binary_available;
  facts.service_enabled = false;
  facts.service_running = false;
  facts.daemon_ping_ok = false;
  facts.daemon_readiness_ok = false;
  return facts;
}

[[nodiscard]] DesiredConfigSnapshot build_desired_snapshot(
    const DaemonConfigStoreSnapshot& snapshot) {
  DesiredConfigSnapshot desired;
  desired.profile_id = snapshot.profile_id.value_or("unconfigured");
  if (!snapshot.daemon_config_json.empty()) {
    desired.daemon.socket_path = extract_json_string_value(
                                     snapshot.daemon_config_json,
                                     "socket_path")
                                     .value_or(std::string(
                                         dasall::access::daemon::kDefaultDaemonSocketPath));
    desired.daemon.log_format = extract_json_string_value(
                                    snapshot.daemon_config_json,
                                    "log_format")
                                    .value_or("json");
    desired.daemon.diag_enabled = extract_json_bool_value(
                                      snapshot.daemon_config_json,
                                      "diag_enabled")
                                      .value_or(false);
    desired.daemon.override_enabled = extract_json_bool_value(
                                          snapshot.daemon_config_json,
                                          "override_enabled")
                                          .value_or(false);
    desired.daemon.watchdog_enabled = extract_json_bool_value(
                                          snapshot.daemon_config_json,
                                          "watchdog_enabled")
                                          .value_or(false);
  }
  return desired;
}

[[nodiscard]] std::optional<ObservedConfigState> load_observed_state(
    const DaemonConfigFileStore& file_store,
    const ConfigPreflightEnvironment& environment,
    const InstallStateProbe& install_state_probe,
    std::string* error_message) {
  const auto snapshot = file_store.load_current(error_message);
  if (!snapshot.has_value()) {
    return std::nullopt;
  }

  ObservedConfigState state;
  state.snapshot = *snapshot;
  state.desired = build_desired_snapshot(state.snapshot);
  state.facts = build_install_state_facts(state.snapshot, environment);
  state.probe_result = install_state_probe.probe(state.facts);
  return state;
}

void append_string_list(std::string& output,
                        std::string_view title,
                        const std::vector<std::string>& values) {
  output += std::string(title);
  output += ":\n";
  if (values.empty()) {
    output += "- (none)\n";
    return;
  }

  for (const auto& value : values) {
    output += "- ";
    output += value;
    output += '\n';
  }
}

[[nodiscard]] ConfigSummaryView build_summary(
    const ObservedConfigState& observed_state) {
  ConfigSummaryView summary;
  summary.profile_id = observed_state.desired.profile_id;
  summary.socket_path = observed_state.desired.daemon.socket_path;
  summary.log_format = observed_state.desired.daemon.log_format;
  summary.service_installed = observed_state.facts.service_installed;
  summary.service_running = observed_state.facts.service_running;
  summary.service_enabled = observed_state.facts.service_enabled;
  summary.ping_status = observed_state.facts.daemon_ping_ok ? "ready" : "unknown";
  summary.readiness_status = observed_state.facts.daemon_readiness_ok ? "ready" : "unknown";
  summary.operator_access_hint = std::string(kOperatorAccessHint);
  summary.incomplete_items = observed_state.probe_result.gaps;
  summary.apply_result.state_before = observed_state.probe_result.state;
  summary.apply_result.state_after = observed_state.probe_result.state;

  switch (observed_state.probe_result.state) {
    case InstallState::FreshInstall:
      summary.next_steps.emplace_back(kSummaryNextStepFreshInstall);
      break;
    case InstallState::BootstrapPending:
      summary.next_steps.emplace_back(kSummaryNextStepBootstrapPending);
      break;
    case InstallState::ConfiguredStopped:
      summary.next_steps.emplace_back(kSummaryNextStepConfiguredStopped);
      break;
    case InstallState::ConfiguredRunning:
      summary.next_steps.emplace_back(kSummaryNextStepConfiguredRunning);
      break;
    case InstallState::Drifted:
      summary.next_steps.emplace_back(kSummaryNextStepDrifted);
      break;
    case InstallState::Unsupported:
      add_unique_string(summary.incomplete_items, "systemd_unavailable");
      break;
  }

  return summary;
}

[[nodiscard]] ConfigActionPlan build_read_only_plan(
    const ObservedConfigState& observed_state) {
  ConfigActionPlan plan;
  plan.state_before = observed_state.probe_result.state;
  plan.state_after_expected = observed_state.probe_result.state;
  plan.service_validate_requested =
      observed_state.snapshot.profile_id.has_value() &&
      observed_state.snapshot.daemon_config_file_exists;
  plan.blocked_actions = observed_state.probe_result.gaps;

  switch (observed_state.probe_result.state) {
    case InstallState::FreshInstall:
      plan.manual_followups.emplace_back(kSummaryNextStepFreshInstall);
      break;
    case InstallState::BootstrapPending:
      plan.manual_followups.emplace_back(kSummaryNextStepBootstrapPending);
      break;
    case InstallState::ConfiguredStopped:
      plan.manual_followups.emplace_back(kSummaryNextStepConfiguredStopped);
      break;
    case InstallState::ConfiguredRunning:
      plan.manual_followups.emplace_back(kSummaryNextStepConfiguredRunning);
      break;
    case InstallState::Drifted:
      plan.manual_followups.emplace_back(kSummaryNextStepDrifted);
      break;
    case InstallState::Unsupported:
      add_unique_string(plan.blocked_actions, "systemd_unavailable");
      break;
  }

  return plan;
}

[[nodiscard]] std::string join_command(
    const std::vector<std::string>& command) {
  if (command.empty()) {
    return "(none)";
  }

  std::string output;
  for (std::size_t index = 0; index < command.size(); ++index) {
    if (index != 0) {
      output += ' ';
    }
    output += command[index];
  }
  return output;
}

[[nodiscard]] int exit_code_for_preflight(
    const ConfigPreflightResult& preflight_result) {
  if (preflight_result.has_reason("root_required_for_config_write")) {
    return 4;
  }
  if (preflight_result.has_reason("daemon_binary_unavailable") ||
      preflight_result.has_reason("daemon_validate_only_unavailable")) {
    return 3;
  }
  if (preflight_result.has_reason("daemon_validate_only_failed") ||
      preflight_result.has_reason("non_canonical_socket_path") ||
      preflight_result.has_reason("canonical_defaults_not_writable") ||
      preflight_result.has_reason("canonical_daemon_config_not_writable")) {
    return 5;
  }
  return 7;
}

  [[nodiscard]] ConfigSummaryView build_apply_summary(
    const DesiredConfigSnapshot& desired,
    const InstallStateFacts& facts,
    const ConfigActionPlan& plan,
    const ConfigApplyResult& apply_result);

  [[nodiscard]] std::string format_apply_output(
    const dasall::apps::cli::CliCommand& command,
    const ConfigSummaryView& summary);

  [[nodiscard]] CliConfigWorkflowResult make_failure_result(
    const dasall::apps::cli::CliCommand& command,
    CliConfigWorkflowStatus status,
    int exit_code,
    std::string reason);

[[nodiscard]] std::string join_text_values(
    const std::vector<std::string>& values) {
  if (values.empty()) {
    return "(none)";
  }

  std::string output;
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index != 0) {
      output += ", ";
    }
    output += values[index];
  }
  return output;
}

[[nodiscard]] std::string wizard_state_banner(
    const ObservedConfigState& observed_state) {
  std::string banner = "[WelcomeAndStatePage]\ncurrent_state: ";
  banner += std::string(to_string(observed_state.probe_result.state));
  banner += "\ngaps: ";
  banner += join_text_values(observed_state.probe_result.gaps);
  return banner;
}

[[nodiscard]] std::string prompt_label_with_banner(
    const ObservedConfigState& observed_state,
    std::string_view page_name,
    std::string_view message) {
  std::string output = wizard_state_banner(observed_state);
  output += "\n[";
  output += page_name;
  output += "]\n";
  output += message;
  return output;
}

[[nodiscard]] std::string wizard_profile_default(
    const ObservedConfigState& observed_state) {
  if (observed_state.desired.profile_id.empty() ||
      observed_state.desired.profile_id == "unconfigured") {
    return "desktop_full";
  }
  return observed_state.desired.profile_id;
}

[[nodiscard]] DesiredConfigSnapshot collect_wizard_desired_snapshot(
    const InteractivePromptEngine& prompt_engine,
    const ObservedConfigState& observed_state) {
  DesiredConfigSnapshot desired = observed_state.desired;
  desired.profile_id = prompt_engine
                           .prompt_text(
                               "profile_id",
                               prompt_label_with_banner(
                                   observed_state,
                                   "ProfileSelectionPage",
                                   "Select DASALL profile"),
                               wizard_profile_default(observed_state))
                           .value;
  desired.daemon.socket_path = prompt_engine
                                   .prompt_text(
                                       "daemon.socket_path",
                                       prompt_label_with_banner(
                                           observed_state,
                                           "DaemonConfigPage",
                                           "Enter daemon socket path"),
                                       desired.daemon.socket_path)
                                   .value;
  desired.daemon.log_format = prompt_engine
                                  .prompt_text(
                                      "daemon.log_format",
                                      "[DaemonConfigPage]\nEnter daemon log format",
                                      desired.daemon.log_format)
                                  .value;
  desired.daemon.diag_enabled = prompt_engine.prompt_confirm(
      "[DaemonConfigPage]\nEnable daemon diagnostic logging?",
      desired.daemon.diag_enabled);
  desired.daemon.override_enabled = prompt_engine.prompt_confirm(
      "[DaemonConfigPage]\nAllow deployment override inputs?",
      desired.daemon.override_enabled);
  desired.daemon.watchdog_enabled = prompt_engine.prompt_confirm(
      "[DaemonConfigPage]\nEnable daemon watchdog integration?",
      desired.daemon.watchdog_enabled);
  desired.service.start_now = prompt_engine.prompt_confirm(
      "[ServiceActionPage]\nStart dasall-daemon after validation?",
      observed_state.facts.service_running);
  desired.service.enable_on_boot = prompt_engine.prompt_confirm(
      "[ServiceActionPage]\nEnable dasall-daemon.service on boot?",
      observed_state.facts.service_enabled);
  return desired;
}

[[nodiscard]] CliConfigWorkflowResult execute_apply_workflow(
    const dasall::apps::cli::CliCommand& command,
    const DesiredConfigSnapshot& desired,
    const ObservedConfigState& observed_state,
    const ConfigActionPlan& plan,
    const DaemonConfigFileStore& file_store,
    const ConfigPreflightChecker& preflight_checker,
  const ServiceManagerAdapter& service_manager,
    const ValidateOnlyRunner& validate_only_runner,
  const ServiceCommandRunner& service_command_runner,
    std::optional<PrivilegeContext> privilege_context) {
  if (!plan.secret_writes.empty()) {
    return make_failure_result(command,
                               CliConfigWorkflowStatus::ApplyRendered,
                               6,
                               std::string(kApplySecretWritesBlocked));
  }

  ConfigActionPlan preflight_plan = plan;
  preflight_plan.service_validate_requested = false;
  const auto preflight_result = preflight_checker.run(
      preflight_plan,
      desired,
      validate_only_runner,
      privilege_context);
  if (!preflight_result.ok) {
    ConfigApplyResult apply_result;
    apply_result.outcome = kConfigApplyOutcomeBlocked;
    apply_result.state_before = observed_state.probe_result.state;
    apply_result.state_after = observed_state.probe_result.state;
    apply_result.blocked_actions = preflight_result.failure_reasons;
    ConfigSummaryView summary = build_apply_summary(desired,
                                                   observed_state.facts,
                                                   plan,
                                                   apply_result);
    return CliConfigWorkflowResult{
        .handled = true,
        .success = false,
        .status = CliConfigWorkflowStatus::ApplyRendered,
        .exit_code = exit_code_for_preflight(preflight_result),
        .command_name = std::string(workflow_command_name(command.config_command)),
        .output = format_apply_output(command, summary),
        .failure_reason = "config apply preflight failed",
    };
  }

  const auto write_result = file_store.write_desired(desired);
  ConfigApplyResult apply_result;
  apply_result.state_before = observed_state.probe_result.state;
  apply_result.state_after = plan.state_after_expected;
  for (const auto& operation : write_result.transaction.operations) {
    apply_result.written_files.push_back(operation.target_path.string());
  }

  if (!write_result.success) {
    apply_result.outcome = write_result.rolled_back
                               ? kConfigApplyOutcomeFailedRolledBack
                               : kConfigApplyOutcomeBlocked;
    apply_result.rollback_performed = write_result.rolled_back;
    apply_result.blocked_actions.push_back(write_result.error_message);
    ConfigSummaryView summary = build_apply_summary(desired,
                                                   observed_state.facts,
                                                   plan,
                                                   apply_result);
    return CliConfigWorkflowResult{
        .handled = true,
        .success = false,
        .status = CliConfigWorkflowStatus::ApplyRendered,
        .exit_code = 5,
        .command_name = std::string(workflow_command_name(command.config_command)),
        .output = format_apply_output(command, summary),
        .failure_reason = write_result.error_message,
    };
  }

  if (plan.service_validate_requested) {
    ConfigActionPlan validate_plan;
    validate_plan.service_validate_requested = true;
    const auto validate_result = preflight_checker.run(
        validate_plan,
        desired,
        validate_only_runner,
        privilege_context);
    if (!validate_result.ok) {
      std::string rollback_error;
      apply_result.rollback_performed = file_store.rollback_last_write(
          write_result.transaction, &rollback_error);
      apply_result.outcome = kConfigApplyOutcomeFailedRolledBack;
      apply_result.blocked_actions = validate_result.failure_reasons;
      if (!rollback_error.empty()) {
        apply_result.blocked_actions.push_back(rollback_error);
      }
      ConfigSummaryView summary = build_apply_summary(desired,
                                                     observed_state.facts,
                                                     plan,
                                                     apply_result);
      return CliConfigWorkflowResult{
          .handled = true,
          .success = false,
          .status = CliConfigWorkflowStatus::ApplyRendered,
          .exit_code = exit_code_for_preflight(validate_result),
          .command_name = std::string(workflow_command_name(command.config_command)),
          .output = format_apply_output(command, summary),
          .failure_reason = "config apply validate-only failed after writing canonical files",
      };
    }
  }

  const auto service_execution_plan = service_manager.plan_service_actions(
      plan, observed_state.facts.systemd_available);
  const auto service_apply_result = service_manager.apply(
      service_execution_plan,
      service_command_runner);
  apply_result.completed_actions = service_apply_result.completed_actions;
  apply_result.manual_followups = service_apply_result.manual_followups;
  apply_result.blocked_actions.insert(apply_result.blocked_actions.end(),
                                      service_apply_result.blocked_actions.begin(),
                                      service_apply_result.blocked_actions.end());
  if (plan.service_restart_required || plan.service_start_requested) {
    apply_result.state_after =
        contains_string(apply_result.completed_actions, "restart") ||
                contains_string(apply_result.completed_actions, "start")
            ? InstallState::ConfiguredRunning
            : InstallState::ConfiguredStopped;
  }
  if (!service_apply_result.success) {
    apply_result.outcome = kConfigApplyOutcomeBlocked;
    apply_result.applied = true;
    add_unique_string(apply_result.manual_followups,
                      "systemctl status dasall-daemon.service");
    add_unique_string(apply_result.manual_followups,
                      "journalctl -u dasall-daemon.service -n 50 --no-pager");
    apply_result.blocked_actions.push_back(service_apply_result.error_message);
    ConfigSummaryView summary = build_apply_summary(desired,
                                                   observed_state.facts,
                                                   plan,
                                                   apply_result);
    return CliConfigWorkflowResult{
        .handled = true,
        .success = false,
        .status = CliConfigWorkflowStatus::ApplyRendered,
        .exit_code = 6,
        .command_name = std::string(workflow_command_name(command.config_command)),
        .output = format_apply_output(command, summary),
        .failure_reason = service_apply_result.error_message,
    };
  }

  apply_result.outcome = kConfigApplyOutcomeApplied;
  apply_result.applied = true;
  ConfigSummaryView summary = build_apply_summary(desired,
                                                 observed_state.facts,
                                                 plan,
                                                 apply_result);
  return CliConfigWorkflowResult{
      .handled = true,
      .success = true,
      .status = CliConfigWorkflowStatus::ApplyRendered,
      .exit_code = 0,
      .command_name = std::string(workflow_command_name(command.config_command)),
      .output = format_apply_output(command, summary),
      .failure_reason = {},
  };
}

[[nodiscard]] ConfigSummaryView build_apply_summary(
    const DesiredConfigSnapshot& desired,
    const InstallStateFacts& facts,
    const ConfigActionPlan& plan,
    const ConfigApplyResult& apply_result) {
  ConfigSummaryView summary;
  summary.profile_id = desired.profile_id;
  summary.socket_path = desired.daemon.socket_path;
  summary.log_format = desired.daemon.log_format;
  summary.service_installed = facts.service_installed;
  summary.service_running = facts.service_running ||
                            contains_string(apply_result.completed_actions,
                                            "restart") ||
                            contains_string(apply_result.completed_actions,
                                            "start");
  summary.service_enabled = facts.service_enabled ||
                            contains_string(apply_result.completed_actions,
                                            "enable");
  summary.operator_access_hint = std::string(kOperatorAccessHint);
  summary.incomplete_items = plan.blocked_actions;
  summary.next_steps = apply_result.manual_followups;
  summary.apply_result = apply_result;
  return summary;
}

[[nodiscard]] std::string format_apply_output(
    const dasall::apps::cli::CliCommand& command,
    const ConfigSummaryView& summary) {
  return render_for_output_mode(command,
                                ConfigSummaryFormatter::format_human(summary),
                                ConfigSummaryFormatter::format_json(summary));
}

[[nodiscard]] std::string format_validation_human(
    const InstallState state_before,
    const ConfigPreflightResult& preflight_result) {
  std::string output = "[dasall-config] validate\n";
  output += "state_before: ";
  output += std::string(to_string(state_before));
  output += "\nvalidation: ";
  output += preflight_result.ok ? "passed" : "failed";
  output += "\nroot_required: ";
  output += bool_text(preflight_result.root_required);
  output += "\nvalidate_only_command: ";
  output += join_command(preflight_result.validate_only_command);
  output += '\n';
  append_string_list(output, "failure_reasons", preflight_result.failure_reasons);
  append_string_list(output, "manual_followups", preflight_result.manual_followups);
  append_string_list(output, "blocked_actions", preflight_result.blocked_actions);
  return output;
}

[[nodiscard]] std::string format_validation_json(
    const InstallState state_before,
    const ConfigPreflightResult& preflight_result) {
  std::string output = "{";
  output += "\"schema_version\":\"dasall.config.validate.v1\"";
  output += ",\"state_before\":" +
            json_string(to_string(state_before));
  output += ",\"ok\":" + bool_text(preflight_result.ok);
  output += ",\"root_required\":" +
            bool_text(preflight_result.root_required);
  output += ",\"running_as_root\":" +
            bool_text(preflight_result.running_as_root);
  output += ",\"stdin_is_tty\":" +
            bool_text(preflight_result.stdin_is_tty);
  output += ",\"systemd_available\":" +
            bool_text(preflight_result.systemd_available);
  output += ",\"validate_only_passed\":" +
            bool_text(preflight_result.validate_only_passed);
  output += ",\"validate_only_command\":" +
            json_string_array(preflight_result.validate_only_command);
  output += ",\"failure_reasons\":" +
            json_string_array(preflight_result.failure_reasons);
  output += ",\"manual_followups\":" +
            json_string_array(preflight_result.manual_followups);
  output += ",\"blocked_actions\":" +
            json_string_array(preflight_result.blocked_actions);
  output += '}';
  return output;
}

[[nodiscard]] CliConfigWorkflowResult make_failure_result(
    const dasall::apps::cli::CliCommand& command,
    const CliConfigWorkflowStatus status,
    const int exit_code,
    std::string reason) {
  CliConfigWorkflowResult result;
  result.handled = true;
  result.success = false;
  result.status = status;
  result.exit_code = exit_code;
  result.command_name = std::string(workflow_command_name(command.config_command));
  result.failure_reason = std::move(reason);
  if (command.output_mode == dasall::apps::cli::CliOutputMode::Json) {
    result.output = "{";
    result.output += "\"command\":" + json_string(result.command_name);
    result.output += ",\"ok\":false";
    result.output += ",\"exit_code\":" + std::to_string(exit_code);
    result.output += ",\"failure_reason\":" +
                     json_string(result.failure_reason);
    result.output += '}';
  } else {
    result.output = result.failure_reason;
  }
  return result;
}

}  // namespace

CliConfigWorkflowCoordinator::CliConfigWorkflowCoordinator(
    CliConfigWorkflowDependencies dependencies)
    : dependencies_(std::move(dependencies)),
      file_store_(dependencies_.store_paths),
      preflight_checker_([&]() {
        auto environment = dependencies_.preflight_environment;
        environment.defaults_file = dependencies_.store_paths.defaults_file;
        environment.daemon_config_file = dependencies_.store_paths.daemon_config_file;
        return environment;
      }()),
      prompt_engine_(dependencies_.prompt_input_handler,
                     dependencies_.prompt_confirm_handler) {
  if (!dependencies_.validate_only_runner) {
    dependencies_.validate_only_runner = default_validate_only_runner;
  }
  if (!dependencies_.service_command_runner) {
    dependencies_.service_command_runner = default_service_command_runner;
  }
}

CliConfigWorkflowResult CliConfigWorkflowCoordinator::run(
    const dasall::apps::cli::CliCommand& command) const {
  if (command.name != "config") {
    return CliConfigWorkflowResult{
        .handled = false,
        .success = false,
        .status = CliConfigWorkflowStatus::UnsupportedCommand,
        .exit_code = 7,
        .command_name = {},
        .output = {},
        .failure_reason = "not a config workflow command",
    };
  }

  switch (command.config_command) {
    case dasall::apps::cli::CliConfigCommandKind::Show: {
      std::string error_message;
      const auto observed_state = load_observed_state(
          file_store_, dependencies_.preflight_environment,
          install_state_probe_, &error_message);
      if (!observed_state.has_value()) {
        return make_failure_result(command,
                                   CliConfigWorkflowStatus::WorkflowFailed,
                                   5,
                                   std::move(error_message));
      }
      return render_summary(command, build_summary(*observed_state));
    }
    case dasall::apps::cli::CliConfigCommandKind::Plan: {
      std::string error_message;
      const auto observed_state = load_observed_state(
          file_store_, dependencies_.preflight_environment,
          install_state_probe_, &error_message);
      if (!observed_state.has_value()) {
        return make_failure_result(command,
                                   CliConfigWorkflowStatus::WorkflowFailed,
                                   5,
                                   std::move(error_message));
      }
      if (command.config_from_file.has_value()) {
        std::string parse_error;
        const auto desired = diff_planner_.load_desired_from_file(
            *command.config_from_file, &parse_error);
        if (!desired.has_value()) {
          return make_failure_result(command,
                                     CliConfigWorkflowStatus::WorkflowFailed,
                                     2,
                                     std::move(parse_error));
        }
        return render_plan(command,
                           diff_planner_.build_plan(observed_state->desired,
                                                    *desired,
                                                    observed_state->probe_result.state,
                                                    file_store_.paths()));
      }

      return render_plan(command, build_read_only_plan(*observed_state));
    }
    case dasall::apps::cli::CliConfigCommandKind::Validate: {
      std::string error_message;
      const auto observed_state = load_observed_state(
          file_store_, dependencies_.preflight_environment,
          install_state_probe_, &error_message);
      if (!observed_state.has_value()) {
        return make_failure_result(command,
                                   CliConfigWorkflowStatus::WorkflowFailed,
                                   5,
                                   std::move(error_message));
      }

      if (!observed_state->snapshot.profile_id.has_value()) {
        return make_failure_result(command,
                                   CliConfigWorkflowStatus::ValidationRendered,
                                   5,
                                   std::string(kValidateProfileMissing));
      }
      if (!observed_state->snapshot.daemon_config_file_exists) {
        return make_failure_result(command,
                                   CliConfigWorkflowStatus::ValidationRendered,
                                   5,
                                   std::string(kValidateDaemonConfigMissing));
      }

      ConfigActionPlan validate_plan;
      validate_plan.state_before = observed_state->probe_result.state;
      validate_plan.state_after_expected = observed_state->probe_result.state;
      validate_plan.service_validate_requested = true;
      const auto preflight_result = preflight_checker_.run(
          validate_plan,
          observed_state->desired,
          dependencies_.validate_only_runner,
          dependencies_.privilege_context);
      return render_validation(command,
                               preflight_result.ok,
                               observed_state->probe_result.state,
                               preflight_result);
    }
    case dasall::apps::cli::CliConfigCommandKind::Apply: {
      if (!command.config_from_file.has_value()) {
        return make_failure_result(command,
                                   CliConfigWorkflowStatus::ApplyRendered,
                                   2,
                                   std::string(kApplyFromFileMissing));
      }

      std::string parse_error;
      const auto desired = diff_planner_.load_desired_from_file(
          *command.config_from_file, &parse_error);
      if (!desired.has_value()) {
        return make_failure_result(command,
                                   CliConfigWorkflowStatus::ApplyRendered,
                                   2,
                                   std::move(parse_error));
      }

      std::string observe_error;
      const auto observed_state = load_observed_state(
          file_store_, dependencies_.preflight_environment,
          install_state_probe_, &observe_error);
      if (!observed_state.has_value()) {
        return make_failure_result(command,
                                   CliConfigWorkflowStatus::ApplyRendered,
                                   5,
                                   std::move(observe_error));
      }

      const auto plan = diff_planner_.build_plan(observed_state->desired,
                                                 *desired,
                                                 observed_state->probe_result.state,
                                                 file_store_.paths());
      return execute_apply_workflow(command,
                                    *desired,
                                    *observed_state,
                                    plan,
                                    file_store_,
                                    preflight_checker_,
                                    service_manager_,
                                    dependencies_.validate_only_runner,
                                    dependencies_.service_command_runner,
                                    dependencies_.privilege_context);
    }
    case dasall::apps::cli::CliConfigCommandKind::Wizard: {
      if (command.no_input) {
        return make_failure_result(command,
                                   CliConfigWorkflowStatus::WorkflowFailed,
                                   2,
                                   std::string(kWizardNoInputNotAllowed));
      }

      std::string error_message;
      const auto observed_state = load_observed_state(
          file_store_, dependencies_.preflight_environment,
          install_state_probe_, &error_message);
      if (!observed_state.has_value()) {
        return make_failure_result(command,
                                   CliConfigWorkflowStatus::WorkflowFailed,
                                   5,
                                   std::move(error_message));
      }

      const DesiredConfigSnapshot desired = collect_wizard_desired_snapshot(
          prompt_engine_, *observed_state);
      const auto plan = diff_planner_.build_plan(observed_state->desired,
                                                 desired,
                                                 observed_state->probe_result.state,
                                                 file_store_.paths());
      std::string review_message = "[ReviewAndApplyPage]\n";
      review_message += ConfigPlanFormatter::format_human(plan);
      review_message += "\n[OperatorAccessPage]\n";
      review_message += std::string(kOperatorAccessHint);
      review_message += "\nApply the generated plan now?";
      if (!prompt_engine_.prompt_confirm(review_message, false)) {
        return make_failure_result(command,
                                   CliConfigWorkflowStatus::WorkflowFailed,
                                   2,
                                   std::string(kWizardApplyCancelled));
      }

      return execute_apply_workflow(command,
                                    desired,
                                    *observed_state,
                                    plan,
                                    file_store_,
                                    preflight_checker_,
                                    service_manager_,
                                    dependencies_.validate_only_runner,
                                    dependencies_.service_command_runner,
                                    dependencies_.privilege_context);
    }
    case dasall::apps::cli::CliConfigCommandKind::None:
      break;
  }

  return CliConfigWorkflowResult{
      .handled = true,
      .success = false,
      .status = CliConfigWorkflowStatus::PendingImplementation,
      .exit_code = 7,
      .command_name = std::string(workflow_command_name(command.config_command)),
      .output = render_for_output_mode(command,
                                       pending_reason(command.config_command),
                                       std::string("{\"command\":") +
                                           json_string(workflow_command_name(command.config_command)) +
                                           ",\"ok\":false,\"failure_reason\":" +
                                           json_string(pending_reason(command.config_command)) + "}"),
      .failure_reason = pending_reason(command.config_command),
  };
}

CliConfigWorkflowResult CliConfigWorkflowCoordinator::render_plan(
    const dasall::apps::cli::CliCommand& command,
    const ConfigActionPlan& plan) const {
  if (command.config_command != dasall::apps::cli::CliConfigCommandKind::Plan) {
    return CliConfigWorkflowResult{
        .handled = true,
        .success = false,
        .status = CliConfigWorkflowStatus::UnsupportedCommand,
      .exit_code = 7,
        .command_name = std::string(workflow_command_name(command.config_command)),
        .output = {},
        .failure_reason = "plan rendering requires the config plan subcommand",
    };
  }

  return CliConfigWorkflowResult{
      .handled = true,
      .success = true,
      .status = CliConfigWorkflowStatus::PlanRendered,
      .exit_code = 0,
      .command_name = std::string(workflow_command_name(command.config_command)),
      .output = render_for_output_mode(command,
                                       ConfigPlanFormatter::format_human(plan),
                                       ConfigPlanFormatter::format_json(plan)),
      .failure_reason = {},
  };
}

CliConfigWorkflowResult CliConfigWorkflowCoordinator::render_summary(
    const dasall::apps::cli::CliCommand& command,
    const ConfigSummaryView& summary) const {
  if (command.config_command != dasall::apps::cli::CliConfigCommandKind::Show) {
    return CliConfigWorkflowResult{
        .handled = true,
        .success = false,
        .status = CliConfigWorkflowStatus::UnsupportedCommand,
      .exit_code = 7,
        .command_name = std::string(workflow_command_name(command.config_command)),
        .output = {},
        .failure_reason = "summary rendering requires the config show subcommand",
    };
  }

  return CliConfigWorkflowResult{
      .handled = true,
      .success = true,
      .status = CliConfigWorkflowStatus::SummaryRendered,
      .exit_code = 0,
      .command_name = std::string(workflow_command_name(command.config_command)),
      .output = render_for_output_mode(
          command,
          ConfigSummaryFormatter::format_human(summary),
          ConfigSummaryFormatter::format_json(summary)),
      .failure_reason = {},
  };
}

CliConfigWorkflowResult CliConfigWorkflowCoordinator::render_validation(
    const dasall::apps::cli::CliCommand& command,
    const bool success,
    const InstallState state_before,
    const ConfigPreflightResult& preflight_result) const {
  if (command.config_command != dasall::apps::cli::CliConfigCommandKind::Validate) {
    return CliConfigWorkflowResult{
        .handled = true,
        .success = false,
        .status = CliConfigWorkflowStatus::UnsupportedCommand,
        .exit_code = 7,
        .command_name = std::string(workflow_command_name(command.config_command)),
        .output = {},
        .failure_reason = "validation rendering requires the config validate subcommand",
    };
  }

  return CliConfigWorkflowResult{
      .handled = true,
      .success = success,
      .status = CliConfigWorkflowStatus::ValidationRendered,
      .exit_code = success ? 0 : exit_code_for_preflight(preflight_result),
      .command_name = std::string(workflow_command_name(command.config_command)),
      .output = render_for_output_mode(command,
                                       format_validation_human(state_before,
                                                               preflight_result),
                                       format_validation_json(state_before,
                                                              preflight_result)),
      .failure_reason = success ? std::string() : std::string("config validate failed"),
  };
}

}  // namespace dasall::apps::cli::config