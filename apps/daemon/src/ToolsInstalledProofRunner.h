#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace dasall::apps::daemon {

struct ToolsInstalledProofOptions {
  std::string requested_profile_id;
  std::optional<std::filesystem::path> deployment_config_path;
  std::optional<std::filesystem::path> readonly_assets_root_override;
  std::optional<std::filesystem::path> state_root_override;
};

struct ToolsInstalledProofResult {
  std::string effective_profile_id;
  std::string route_kind;
  std::string payload;
  std::string terminal_route_kind;
  std::string terminal_payload;
  std::string observation_id;
  std::string observation_digest_summary;
  std::vector<std::string> visible_tools;
  std::vector<std::string> external_evidence;
  bool agent_dataset_visible = false;
  bool agent_terminal_visible = false;
  bool tool_invocation_succeeded = false;
  bool terminal_confirmation_denied = false;
  bool terminal_invocation_succeeded = false;
  bool projection_present = false;
  bool terminal_projection_present = false;
  bool route_citation_present = false;
  bool tool_call_citation_present = false;
  bool production_bridge_evidence_present = false;
  bool production_observability_evidence_present = false;
  std::string failure_reason_code;
  std::string terminal_failure_reason_code;
  std::string error;

  [[nodiscard]] bool ok() const {
    return error.empty() && !effective_profile_id.empty() && agent_dataset_visible &&
           agent_terminal_visible && tool_invocation_succeeded &&
           terminal_confirmation_denied && terminal_invocation_succeeded &&
           projection_present && terminal_projection_present &&
           route_citation_present && tool_call_citation_present &&
           route_kind == "builtin" && terminal_route_kind == "builtin" &&
           !payload.empty() && !terminal_payload.empty() &&
           !observation_digest_summary.empty() &&
           production_bridge_evidence_present &&
           production_observability_evidence_present;
  }
};

[[nodiscard]] ToolsInstalledProofResult collect_tools_installed_proof(
    const ToolsInstalledProofOptions& options = {});

}  // namespace dasall::apps::daemon