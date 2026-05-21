#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace dasall::apps::daemon {

struct RuntimeInstalledProofOptions {
  std::string requested_profile_id;
  std::optional<std::filesystem::path> deployment_config_path;
  std::optional<std::filesystem::path> readonly_assets_root_override;
  std::optional<std::filesystem::path> state_root_override;
};

struct RuntimeInstalledProofResult {
  std::string effective_profile_id;
  std::vector<std::string> visible_tools;
  std::vector<std::string> external_evidence;
  std::string tool_init_readiness;
  std::string tool_status;
  bool tool_task_completed = false;
  std::string tool_runtime_path;
  std::string tool_checkpoint_ref;
  std::string tool_response_text;
  std::string recovery_init_readiness;
  std::string waiting_status;
  std::string waiting_checkpoint_ref;
  std::string recovery_positive_status;
  bool recovery_positive_task_completed = false;
  std::string recovery_positive_runtime_path;
  std::string recovery_positive_checkpoint_ref;
  bool recovery_positive_checkpoint_persisted = false;
  std::string recovery_positive_response_text;
  std::string recovery_negative_init_readiness;
  std::string recovery_negative_status;
  bool recovery_negative_task_completed = false;
  bool recovery_negative_binding_rejected = false;
  std::string recovery_negative_response_text;
  bool agent_dataset_visible = false;
  bool agent_terminal_visible = false;
  std::string error;

  [[nodiscard]] bool ok() const {
    return error.empty() && !effective_profile_id.empty() &&
           agent_dataset_visible && agent_terminal_visible &&
           tool_status == "Completed" && tool_task_completed &&
           tool_runtime_path == "runtime_path:tool_positive" &&
           !tool_checkpoint_ref.empty() &&
           waiting_status == "PartiallyCompleted" &&
           !waiting_checkpoint_ref.empty() &&
           recovery_positive_status == "Completed" &&
           recovery_positive_task_completed &&
           recovery_positive_runtime_path == "runtime_path:recovery_positive" &&
           !recovery_positive_checkpoint_ref.empty() &&
           recovery_positive_checkpoint_persisted &&
           recovery_negative_status == "Failed" &&
           !recovery_negative_task_completed &&
           recovery_negative_binding_rejected;
  }
};

[[nodiscard]] RuntimeInstalledProofResult collect_runtime_installed_proof(
    const RuntimeInstalledProofOptions& options = {});

}  // namespace dasall::apps::daemon