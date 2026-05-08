#include "config/ConfigCapabilityResolver.h"

namespace dasall::apps::cli::config {

namespace {

constexpr std::string_view kReasonValidateUnavailable =
    "daemon_validate_only_unavailable";
constexpr std::string_view kReasonSystemdUnavailable = "systemd_unavailable";
constexpr std::string_view kReasonSecretBootstrapUnavailable =
    "secret_bootstrap_writer_unavailable";
constexpr std::string_view kReasonSecretBackendUnavailable =
    "secret_backend_unavailable";
constexpr std::string_view kReasonToolSkillSurfaceHidden =
    "tool_skill_surface_hidden";

void add_reason_if_missing(std::vector<std::string>& reasons,
                           std::string_view reason) {
  for (const auto& existing : reasons) {
    if (existing == reason) {
      return;
    }
  }

  reasons.emplace_back(reason);
}

}  // namespace

std::string_view to_string(const ToolSkillPageMode mode) {
  switch (mode) {
    case ToolSkillPageMode::Hidden:
      return "hidden";
    case ToolSkillPageMode::SummaryOnly:
      return "summary_only";
    case ToolSkillPageMode::Editable:
      return "editable";
  }

  return "hidden";
}

bool ConfigCapabilitySet::has_reason(const std::string_view reason) const {
  for (const auto& existing : unavailable_reasons) {
    if (existing == reason) {
      return true;
    }
  }

  return false;
}

ConfigCapabilitySet ConfigCapabilityResolver::resolve(
    const ConfigCapabilityInputs& inputs) const {
  ConfigCapabilitySet capabilities;
  capabilities.config_validate_available = inputs.daemon_validate_only_available;
  capabilities.service_manager_actions_available = inputs.systemd_available;
  capabilities.llm_secret_onboarding_available =
      inputs.secret_bootstrap_writer_available && inputs.secret_backend_available;

  if (inputs.tool_skill_operator_surface_ready) {
    capabilities.tool_skill_page_mode = ToolSkillPageMode::Editable;
  } else if (inputs.active_tooling_detected) {
    capabilities.tool_skill_page_mode = ToolSkillPageMode::SummaryOnly;
  } else {
    capabilities.tool_skill_page_mode = ToolSkillPageMode::Hidden;
  }

  if (!inputs.daemon_validate_only_available) {
    add_reason_if_missing(capabilities.unavailable_reasons,
                          kReasonValidateUnavailable);
  }

  if (!inputs.systemd_available) {
    add_reason_if_missing(capabilities.unavailable_reasons,
                          kReasonSystemdUnavailable);
  }

  if (!inputs.secret_bootstrap_writer_available) {
    add_reason_if_missing(capabilities.unavailable_reasons,
                          kReasonSecretBootstrapUnavailable);
  }

  if (!inputs.secret_backend_available) {
    add_reason_if_missing(capabilities.unavailable_reasons,
                          kReasonSecretBackendUnavailable);
  }

  if (capabilities.tool_skill_page_mode == ToolSkillPageMode::Hidden) {
    add_reason_if_missing(capabilities.unavailable_reasons,
                          kReasonToolSkillSurfaceHidden);
  }

  return capabilities;
}

}  // namespace dasall::apps::cli::config