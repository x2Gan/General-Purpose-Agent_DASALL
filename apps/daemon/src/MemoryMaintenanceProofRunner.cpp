#include "MemoryMaintenanceProofRunner.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include <sqlite3.h>

#include "BuildProfileResolver.h"
#include "DaemonEntryConfigLoader.h"
#include "IMemoryManager.h"
#include "ProfileCatalog.h"
#include "config/InstallLayout.h"
#include "config/MemoryConfigProjector.h"

namespace dasall::apps::daemon {
namespace {

namespace fs = std::filesystem;

constexpr int kSqliteBusyTimeoutMs = 5000;

[[nodiscard]] std::int64_t current_time_millis() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

[[nodiscard]] std::string select_profile_id(
    const MemoryMaintenanceProofOptions& options) {
  return options.requested_profile_id.empty() ? kDefaultDaemonEntryProfileId
                                              : options.requested_profile_id;
}

[[nodiscard]] fs::path select_state_root(
    const infra::config::InstallLayout& install_layout,
    const MemoryMaintenanceProofOptions& options) {
  return options.state_root_override.has_value() ? *options.state_root_override
                                                 : install_layout.state_root;
}

[[nodiscard]] std::string escape_json_string(const std::string& value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (const char current : value) {
    switch (current) {
      case '\\':
        escaped += "\\\\";
        break;
      case '"':
        escaped += "\\\"";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        escaped.push_back(current);
        break;
    }
  }
  return escaped;
}

[[nodiscard]] std::string encode_string_array_json(
    const std::vector<std::string>& values) {
  std::ostringstream stream;
  stream << "[";
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index != 0U) {
      stream << ",";
    }
    stream << '"' << escape_json_string(values[index]) << '"';
  }
  stream << "]";
  return stream.str();
}

struct ScopedSqliteConnection {
  sqlite3* handle = nullptr;

  ~ScopedSqliteConnection() {
    if (handle != nullptr) {
      sqlite3_close(handle);
    }
  }
};

[[nodiscard]] bool open_sqlite_connection(const fs::path& database_path,
                                          ScopedSqliteConnection& connection,
                                          std::string& error) {
  if (sqlite3_open(database_path.string().c_str(), &connection.handle) != SQLITE_OK) {
    error = std::string("failed to open sqlite database: ") +
            sqlite3_errmsg(connection.handle);
    return false;
  }
  sqlite3_busy_timeout(connection.handle, kSqliteBusyTimeoutMs);
  return true;
}

[[nodiscard]] bool exec_sql(sqlite3* connection,
                            const std::string& sql,
                            std::string& error) {
  char* message = nullptr;
  const int status = sqlite3_exec(connection, sql.c_str(), nullptr, nullptr, &message);
  if (status == SQLITE_OK) {
    return true;
  }

  error = message == nullptr ? std::string("sqlite exec failed")
                             : std::string(message);
  sqlite3_free(message);
  return false;
}

[[nodiscard]] bool insert_session(sqlite3* connection,
                                  const std::string& session_id,
                                  const std::string& turn_ids_json,
                                  const std::int64_t created_at,
                                  const std::int64_t last_active_at,
                                  std::string& error) {
  sqlite3_stmt* statement = nullptr;
  constexpr auto kSql =
      "INSERT INTO sessions(session_id, user_id, latest_summary_memory_ref, metadata_digest, "
      "turn_ids_json, created_at, last_active_at, tags_json) "
      "VALUES(?1, ?2, NULL, NULL, ?3, ?4, ?5, '[]')";
  if (sqlite3_prepare_v2(connection, kSql, -1, &statement, nullptr) != SQLITE_OK) {
    error = "failed to prepare session insert";
    return false;
  }

  sqlite3_bind_text(statement, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(statement, 2, "maintenance-proof-user", -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(statement, 3, turn_ids_json.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(statement, 4, created_at);
  sqlite3_bind_int64(statement, 5, last_active_at);

  const int status = sqlite3_step(statement);
  sqlite3_finalize(statement);
  if (status == SQLITE_DONE) {
    return true;
  }

  error = std::string("failed to insert session: ") + sqlite3_errstr(status);
  return false;
}

[[nodiscard]] bool insert_turn(sqlite3* connection,
                               const std::string& session_id,
                               const std::string& turn_id,
                               const std::int64_t created_at,
                               std::string& error) {
  sqlite3_stmt* statement = nullptr;
  constexpr auto kSql =
      "INSERT INTO turns(turn_id, session_id, user_input, agent_response, tool_call_refs_json, "
      "observation_refs_json, summary_memory_ref, created_at, tags_json) "
      "VALUES(?1, ?2, ?3, ?4, '[]', '[]', NULL, ?5, '[]')";
  if (sqlite3_prepare_v2(connection, kSql, -1, &statement, nullptr) != SQLITE_OK) {
    error = "failed to prepare turn insert";
    return false;
  }

  const std::string user_input = "maintenance-proof-" + turn_id;
  const std::string agent_response = "maintenance-proof-response-" + turn_id;
  sqlite3_bind_text(statement, 1, turn_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(statement, 2, session_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(statement, 3, user_input.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(statement, 4, agent_response.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(statement, 5, created_at);

  const int status = sqlite3_step(statement);
  sqlite3_finalize(statement);
  if (status == SQLITE_DONE) {
    return true;
  }

  error = std::string("failed to insert turn: ") + sqlite3_errstr(status);
  return false;
}

[[nodiscard]] bool insert_summary(sqlite3* connection,
                                  const std::string& session_id,
                                  const std::string& summary_id,
                                  const std::string& source_turn_ids_json,
                                  const std::int64_t created_at,
                                  std::string& error) {
  sqlite3_stmt* statement = nullptr;
  constexpr auto kSql =
      "INSERT INTO summaries(summary_id, session_id, summary_text, source_turn_ids_json, "
      "decisions_made_json, confirmed_facts_json, tool_outcomes_json, created_at, tags_json) "
      "VALUES(?1, ?2, ?3, ?4, '[]', '[]', '[]', ?5, '[]')";
  if (sqlite3_prepare_v2(connection, kSql, -1, &statement, nullptr) != SQLITE_OK) {
    error = "failed to prepare summary insert";
    return false;
  }

  sqlite3_bind_text(statement, 1, summary_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(statement, 2, session_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(statement, 3, "maintenance-proof-protect-oldest-turn", -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_text(statement, 4, source_turn_ids_json.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(statement, 5, created_at);

  const int status = sqlite3_step(statement);
  sqlite3_finalize(statement);
  if (status != SQLITE_DONE) {
    error = std::string("failed to insert summary: ") + sqlite3_errstr(status);
    return false;
  }

  sqlite3_stmt* update_statement = nullptr;
  constexpr auto kUpdateSql =
      "UPDATE sessions SET latest_summary_memory_ref = ?1 WHERE session_id = ?2";
  if (sqlite3_prepare_v2(connection, kUpdateSql, -1, &update_statement, nullptr) != SQLITE_OK) {
    error = "failed to prepare latest summary update";
    return false;
  }
  sqlite3_bind_text(update_statement, 1, summary_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(update_statement, 2, session_id.c_str(), -1, SQLITE_TRANSIENT);
  const int update_status = sqlite3_step(update_statement);
  sqlite3_finalize(update_statement);
  if (update_status == SQLITE_DONE) {
    return true;
  }

  error = std::string("failed to update latest summary reference: ") +
          sqlite3_errstr(update_status);
  return false;
}

[[nodiscard]] bool insert_quarantine(sqlite3* connection,
                                     const std::string& object_id,
                                     std::string& error) {
  sqlite3_stmt* statement = nullptr;
  constexpr auto kSql =
      "INSERT INTO quarantined_records(object_type, object_id, reason, payload_digest, created_at) "
      "VALUES('turn', ?1, 'maintenance-proof-expired', NULL, 1)";
  if (sqlite3_prepare_v2(connection, kSql, -1, &statement, nullptr) != SQLITE_OK) {
    error = "failed to prepare quarantine insert";
    return false;
  }

  sqlite3_bind_text(statement, 1, object_id.c_str(), -1, SQLITE_TRANSIENT);
  const int status = sqlite3_step(statement);
  sqlite3_finalize(statement);
  if (status == SQLITE_DONE) {
    return true;
  }

  error = std::string("failed to insert quarantine row: ") + sqlite3_errstr(status);
  return false;
}

[[nodiscard]] std::optional<int> query_int(sqlite3* connection,
                                           const std::string& sql,
                                           std::string& error) {
  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(connection, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK) {
    error = std::string("failed to prepare integer query: ") + sql;
    return std::nullopt;
  }

  const int step_status = sqlite3_step(statement);
  if (step_status != SQLITE_ROW) {
    sqlite3_finalize(statement);
    error = std::string("integer query returned no row: ") + sql;
    return std::nullopt;
  }

  const int value = sqlite3_column_int(statement, 0);
  sqlite3_finalize(statement);
  return value;
}

[[nodiscard]] std::optional<std::string> query_text(sqlite3* connection,
                                                    const std::string& sql,
                                                    std::string& error) {
  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(connection, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK) {
    error = std::string("failed to prepare text query: ") + sql;
    return std::nullopt;
  }

  const int step_status = sqlite3_step(statement);
  if (step_status != SQLITE_ROW) {
    sqlite3_finalize(statement);
    error = std::string("text query returned no row: ") + sql;
    return std::nullopt;
  }

  const unsigned char* value = sqlite3_column_text(statement, 0);
  const std::string text = value == nullptr ? std::string() : std::string(reinterpret_cast<const char*>(value));
  sqlite3_finalize(statement);
  return text;
}

[[nodiscard]] std::optional<profiles::BuildProfileManifest> resolve_build_manifest(
    const fs::path& profiles_root,
    const profiles::RuntimePolicySnapshot& policy_snapshot,
    std::string& error) {
  const profiles::ProfileCatalog catalog(profiles_root);
  const profiles::BuildProfileResolver resolver(catalog);
  const auto result = resolver.resolve_build_manifest(
      profiles::BuildProfileResolveRequest{
          .profile_id = policy_snapshot.effective_profile_id(),
        .expected_target_platform = std::nullopt,
      });
  if (!result.ok()) {
    error = std::string("build manifest unavailable for profile ") +
            policy_snapshot.effective_profile_id();
    return std::nullopt;
  }

  return result.manifest;
}

[[nodiscard]] std::optional<memory::MemoryConfig> compose_memory_config(
    const profiles::RuntimePolicySnapshot& policy_snapshot,
    const infra::config::InstallLayout& install_layout,
    const fs::path& state_root,
    std::string& error) {
  const auto build_manifest =
      resolve_build_manifest(install_layout.profiles_root, policy_snapshot, error);
  if (!build_manifest.has_value()) {
    return std::nullopt;
  }

  auto memory_config = memory::config::project_memory_config(
      policy_snapshot,
      *build_manifest);
  if (!memory_config.has_value()) {
    error = std::string("memory config projection failed for profile ") +
            policy_snapshot.effective_profile_id();
    return std::nullopt;
  }

  memory_config->storage.db_path = (state_root / "memory" / "memory.db").string();
  memory_config->storage.migrations_dir =
      (install_layout.readonly_assets_root / "sql" / "memory").string();

#if defined(__APPLE__)
  constexpr const char* kSqliteExtensionSuffix = ".dylib";
#else
  constexpr const char* kSqliteExtensionSuffix = ".so";
#endif

  memory_config->vector.sqlite_vss_vector0_path =
      (install_layout.runtime_library_root / "sqlite-vss" /
       (std::string("vector0") + kSqliteExtensionSuffix))
          .string();
  memory_config->vector.sqlite_vss_vss0_path =
      (install_layout.runtime_library_root / "sqlite-vss" /
       (std::string("vss0") + kSqliteExtensionSuffix))
          .string();
  if (memory_config->vector.enabled &&
      (!fs::exists(memory_config->vector.sqlite_vss_vector0_path) ||
       !fs::exists(memory_config->vector.sqlite_vss_vss0_path))) {
    memory_config->vector.enabled = false;
    memory_config->vector.backend_type = memory::VectorBackend::None;
    memory_config->vector.search_top_k = 0;
  }

  return memory_config;
}

[[nodiscard]] std::vector<std::string> make_turn_ids(const std::string& prefix,
                                                     const int turn_count) {
  std::vector<std::string> turn_ids;
  turn_ids.reserve(static_cast<std::size_t>(turn_count));
  for (int index = 1; index <= turn_count; ++index) {
    turn_ids.push_back(prefix + "-turn-" + std::to_string(index));
  }
  return turn_ids;
}

void append_error(std::string& target, const std::string& message) {
  if (!target.empty()) {
    target += "; ";
  }
  target += message;
}

}  // namespace

MemoryMaintenanceProofResult collect_memory_maintenance_proof(
    const MemoryMaintenanceProofOptions& options) {
  MemoryMaintenanceProofResult result;

  const auto install_layout = infra::config::resolve_install_layout();
  const fs::path state_root = select_state_root(install_layout, options);
  std::error_code fs_error;
  fs::create_directories(state_root / "memory", fs_error);
  if (fs_error) {
    result.error = std::string("failed to create memory state root: ") +
                   fs_error.message();
    return result;
  }

  const DaemonEntryConfigLoader entry_loader;
  const auto entry_result = entry_loader.load(DaemonEntryConfigLoadRequest{
      .profiles_root = install_layout.profiles_root,
      .requested_profile_id = select_profile_id(options),
      .deployment_config_path = options.deployment_config_path,
      .socket_path_override = std::nullopt,
  });
  if (!entry_result.ok() || !entry_result.entry_config.has_value()) {
    result.error = std::string("daemon entry config load failed: ") +
                   entry_result.message;
    return result;
  }

  result.effective_profile_id = entry_result.entry_config->effective_profile_id;

  auto memory_config = compose_memory_config(
      *entry_result.entry_config->runtime_policy_snapshot,
      install_layout,
      state_root,
      result.error);
  if (!memory_config.has_value()) {
    return result;
  }

  result.database_path = memory_config->storage.db_path;
  result.retention_turns = memory_config->maintenance.retention_turns;
  if (result.retention_turns <= 0) {
    result.error = "maintenance retention_turns must be positive";
    return result;
  }

  auto manager = memory::create_memory_manager(*memory_config);
  if (manager == nullptr) {
    result.error = "memory manager factory returned null";
    return result;
  }

  const auto init_code = manager->init(*memory_config);
  if (static_cast<int>(init_code) != 0) {
    result.error = std::string("memory manager init failed with code ") +
                   std::to_string(static_cast<int>(init_code));
    return result;
  }

  const auto shutdown_guard = std::shared_ptr<void>(nullptr, [&](void*) {
    manager->shutdown();
  });
  (void)shutdown_guard;

  const auto seed_epoch = current_time_millis();
  const std::string proof_prefix = "maintenance-proof-" + std::to_string(seed_epoch);
  result.session_id = proof_prefix + "-session";
  result.quarantine_object_id = proof_prefix + "-quarantine";

  const int seeded_turn_count = result.retention_turns + 1;
  const auto turn_ids = make_turn_ids(proof_prefix, seeded_turn_count);
  result.protected_turn_id = turn_ids.front();
  result.purged_turn_id = turn_ids.at(1U);
  result.newest_turn_id = turn_ids.back();

  {
    ScopedSqliteConnection connection;
    if (!open_sqlite_connection(result.database_path, connection, result.error)) {
      return result;
    }

    if (!exec_sql(connection.handle, "BEGIN IMMEDIATE", result.error)) {
      return result;
    }

    const auto turn_ids_json = encode_string_array_json(turn_ids);
    if (!insert_session(connection.handle,
                        result.session_id,
                        turn_ids_json,
                        seed_epoch - (seeded_turn_count * 1000),
                        seed_epoch,
                        result.error)) {
      return result;
    }

    for (int index = 0; index < seeded_turn_count; ++index) {
      if (!insert_turn(connection.handle,
                       result.session_id,
                       turn_ids[static_cast<std::size_t>(index)],
                       seed_epoch - (seeded_turn_count * 1000) +
                           (static_cast<std::int64_t>(index) * 1000),
                       result.error)) {
        return result;
      }
    }

    if (!insert_summary(connection.handle,
                        result.session_id,
                        proof_prefix + "-summary",
                        encode_string_array_json({result.protected_turn_id}),
                        seed_epoch + 1,
                        result.error)) {
      return result;
    }

    if (!insert_quarantine(connection.handle, result.quarantine_object_id, result.error)) {
      return result;
    }

    if (!exec_sql(connection.handle, "COMMIT", result.error)) {
      return result;
    }

    const auto turns_before = query_int(
        connection.handle,
        "SELECT COUNT(*) FROM turns WHERE session_id = '" + result.session_id + "'",
        result.error);
    if (!turns_before.has_value()) {
      return result;
    }
    result.turns_before = *turns_before;

    const auto quarantine_before = query_int(
        connection.handle,
        "SELECT COUNT(*) FROM quarantined_records WHERE object_id = '" +
            result.quarantine_object_id + "'",
        result.error);
    if (!quarantine_before.has_value()) {
      return result;
    }
    result.quarantine_rows_before = *quarantine_before;

    const auto journal_mode =
        query_text(connection.handle, "PRAGMA journal_mode;", result.error);
    if (!journal_mode.has_value()) {
      return result;
    }
    result.journal_mode = *journal_mode;
  }

  result.wal_bytes_before = 0;
  const fs::path wal_path = result.database_path.string() + "-wal";
  if (fs::exists(wal_path, fs_error) && !fs_error) {
    result.wal_bytes_before = fs::file_size(wal_path, fs_error);
    if (fs_error) {
      result.wal_bytes_before = 0;
      fs_error.clear();
    }
  }

  result.maintenance_report = manager->run_maintenance(memory::MaintenanceRequest{
      .run_checkpoint = true,
      .run_retention = true,
      .run_quarantine_cleanup = true,
      .run_vector_rebuild = false,
  });

  {
    ScopedSqliteConnection connection;
    if (!open_sqlite_connection(result.database_path, connection, result.error)) {
      return result;
    }

    const auto turns_after = query_int(
        connection.handle,
        "SELECT COUNT(*) FROM turns WHERE session_id = '" + result.session_id + "'",
        result.error);
    if (!turns_after.has_value()) {
      return result;
    }
    result.turns_after = *turns_after;

    const auto quarantine_after = query_int(
        connection.handle,
        "SELECT COUNT(*) FROM quarantined_records WHERE object_id = '" +
            result.quarantine_object_id + "'",
        result.error);
    if (!quarantine_after.has_value()) {
      return result;
    }
    result.quarantine_rows_after = *quarantine_after;

    const auto protected_turn_count = query_int(
        connection.handle,
        "SELECT COUNT(*) FROM turns WHERE turn_id = '" + result.protected_turn_id + "'",
        result.error);
    if (!protected_turn_count.has_value()) {
      return result;
    }
    result.protected_turn_retained = *protected_turn_count == 1;

    const auto purged_turn_count = query_int(
        connection.handle,
        "SELECT COUNT(*) FROM turns WHERE turn_id = '" + result.purged_turn_id + "'",
        result.error);
    if (!purged_turn_count.has_value()) {
      return result;
    }
    result.purged_turn_removed = *purged_turn_count == 0;

    const auto newest_turn_count = query_int(
        connection.handle,
        "SELECT COUNT(*) FROM turns WHERE turn_id = '" + result.newest_turn_id + "'",
        result.error);
    if (!newest_turn_count.has_value()) {
      return result;
    }
    result.newest_turn_retained = *newest_turn_count == 1;
  }

  if (result.turns_before != seeded_turn_count) {
    append_error(result.error,
                 "seeded session did not materialize the expected turn count");
  }
  if (result.quarantine_rows_before != 1) {
    append_error(result.error,
                 "seeded quarantine row count did not equal one");
  }
  if (!result.maintenance_report.checkpoint_executed) {
    append_error(result.error,
                 "maintenance checkpoint did not execute");
  }
  if (result.maintenance_report.checkpoint_wal_pages_remaining != 0) {
    append_error(result.error,
                 "maintenance checkpoint left WAL pages remaining");
  }
  if (result.turns_after != result.retention_turns) {
    append_error(result.error,
                 "turn retention did not converge to the projected retention window");
  }
  if (!result.protected_turn_retained || !result.purged_turn_removed ||
      !result.newest_turn_retained) {
    append_error(result.error,
                 "turn retention did not preserve expected protected/newest rows");
  }
  if (result.quarantine_rows_after != 0) {
    append_error(result.error,
                 "quarantine cleanup did not remove the expired row");
  }
  if (result.maintenance_report.turns_purged < 1) {
    append_error(result.error,
                 "maintenance report did not record any purged turns");
  }
  if (result.maintenance_report.quarantine_cleaned < 1) {
    append_error(result.error,
                 "maintenance report did not record quarantine cleanup");
  }

  return result;
}

}  // namespace dasall::apps::daemon