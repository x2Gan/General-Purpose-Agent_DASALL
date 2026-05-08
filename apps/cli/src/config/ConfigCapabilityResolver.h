#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace dasall::apps::cli::config {

enum class ToolSkillPageMode {
  Hidden,
  SummaryOnly,
  Editable,
};

[[nodiscard]] std::string_view to_string(ToolSkillPageMode mode);

struct ConfigCapabilityInputs {
  bool daemon_validate_only_available = true;
  bool systemd_available = true;
  bool secret_bootstrap_writer_available = false;
  bool secret_backend_available = false;
  bool active_tooling_detected = false;
  bool tool_skill_operator_surface_ready = false;
};

struct ConfigCapabilitySet {
  bool config_validate_available = false;
  bool service_manager_actions_available = false;
  bool llm_secret_onboarding_available = false;
  ToolSkillPageMode tool_skill_page_mode = ToolSkillPageMode::Hidden;
  std::vector<std::string> unavailable_reasons;

  [[nodiscard]] bool has_reason(std::string_view reason) const;
};

class ConfigCapabilityResolver {
 public:
  [[nodiscard]] ConfigCapabilitySet resolve(
      const ConfigCapabilityInputs& inputs) const;
};

}  // namespace dasall::apps::cli::config