#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace dasall::apps::daemon {

struct MemoryInstalledProofOptions {
  std::string requested_profile_id;
  std::optional<std::filesystem::path> deployment_config_path;
  std::optional<std::filesystem::path> state_root_override;
};

struct MemoryInstalledProofResult {
  std::filesystem::path database_path;
  std::string effective_profile_id;
  std::string session_id;
  std::string expected_marker;
  std::string first_turn_id;
  std::string second_turn_id;
  std::string journal_mode;
  int core_table_count = 0;
  int vector_table_count = 0;
  int session_summary_count_after_first = 0;
  int session_turn_count_after_second = 0;
  int session_summary_count_after_second = 0;
  std::string latest_summary_source_turn_ids_json;
  std::string latest_summary_text_prefix;
  bool prepare_context_marker_visible = false;
  bool latest_summary_references_second_turn = false;
  std::string error;

  [[nodiscard]] bool ok() const {
    return error.empty() && database_path.is_absolute() &&
           !effective_profile_id.empty() && !session_id.empty() &&
           !expected_marker.empty() && !first_turn_id.empty() &&
           !second_turn_id.empty() && journal_mode == "wal" &&
           core_table_count >= 5 && vector_table_count >= 1 &&
           session_summary_count_after_first >= 1 &&
           session_turn_count_after_second >= 2 &&
           session_summary_count_after_second >= 2 &&
           prepare_context_marker_visible &&
           latest_summary_references_second_turn &&
           latest_summary_text_prefix.find(expected_marker) != std::string::npos;
  }
};

[[nodiscard]] MemoryInstalledProofResult collect_memory_installed_proof(
    const MemoryInstalledProofOptions& options = {});

}  // namespace dasall::apps::daemon