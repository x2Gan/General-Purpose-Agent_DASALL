#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include "MaintenanceReport.h"

namespace dasall::apps::daemon {

struct MemoryMaintenanceProofOptions {
  std::string requested_profile_id;
  std::optional<std::filesystem::path> deployment_config_path;
  std::optional<std::filesystem::path> state_root_override;
};

struct MemoryMaintenanceProofResult {
  std::filesystem::path database_path;
  std::string effective_profile_id;
  std::string session_id;
  std::string protected_turn_id;
  std::string purged_turn_id;
  std::string newest_turn_id;
  std::string quarantine_object_id;
  std::string journal_mode;
  int retention_turns = 0;
  int turns_before = 0;
  int turns_after = 0;
  int quarantine_rows_before = 0;
  int quarantine_rows_after = 0;
  bool protected_turn_retained = false;
  bool purged_turn_removed = false;
  bool newest_turn_retained = false;
  std::uintmax_t wal_bytes_before = 0;
  memory::MaintenanceReport maintenance_report;
  std::string error;

  [[nodiscard]] bool ok() const {
    return error.empty() && database_path.is_absolute() && !session_id.empty();
  }
};

[[nodiscard]] MemoryMaintenanceProofResult collect_memory_maintenance_proof(
    const MemoryMaintenanceProofOptions& options = {});

}  // namespace dasall::apps::daemon