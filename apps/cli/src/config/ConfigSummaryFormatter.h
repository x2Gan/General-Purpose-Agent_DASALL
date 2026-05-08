#pragma once

#include <string>
#include <vector>

#include "config/ConfigCommandTypes.h"
#include "config/ToolSkillPage.h"

namespace dasall::apps::cli::config {

inline constexpr char kConfigSummarySchemaVersion[] =
    "dasall.config.summary.v1";

struct ConfigSecretSummaryEntry {
  std::string ref;
  std::string status;

  [[nodiscard]] bool is_well_formed() const;
};

struct ConfigSummaryView {
  std::string schema_version = kConfigSummarySchemaVersion;
  std::string profile_id;
  std::string socket_path;
  std::string log_format = "json";
  ToolSkillPageView tool_skill_page;
  std::vector<ConfigSecretSummaryEntry> secret_refs;
  bool service_installed = false;
  bool service_running = false;
  bool service_enabled = false;
  std::string ping_status;
  std::string readiness_status;
  std::string operator_access_hint;
  std::vector<std::string> incomplete_items;
  std::vector<std::string> next_steps;
  ConfigApplyResult apply_result;

  [[nodiscard]] bool is_well_formed() const;
};

class ConfigSummaryFormatter {
 public:
  [[nodiscard]] static std::string format_human(const ConfigSummaryView& summary);

  [[nodiscard]] static std::string format_json(const ConfigSummaryView& summary);
};

}  // namespace dasall::apps::cli::config