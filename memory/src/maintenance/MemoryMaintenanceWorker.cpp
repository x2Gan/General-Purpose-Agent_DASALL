#include "maintenance/MemoryMaintenanceWorker.h"

#include <sqlite3.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "store/sqlite/RowMappers.h"
#include "store/sqlite/SqliteMemoryStore.h"

namespace dasall::memory {
namespace {

[[nodiscard]] std::int64_t current_time_millis() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

void append_warning_once(std::vector<std::string>& warnings,
                         std::string warning) {
  if (std::find(warnings.begin(), warnings.end(), warning) == warnings.end()) {
    warnings.push_back(std::move(warning));
  }
}

[[nodiscard]] bool is_busy_code(int sqlite_status) {
  return sqlite_status == SQLITE_BUSY || sqlite_status == SQLITE_LOCKED;
}

[[nodiscard]] bool begin_transaction(sqlite3* connection) {
  return sqlite3_exec(connection, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr) ==
         SQLITE_OK;
}

[[nodiscard]] bool commit_transaction(sqlite3* connection) {
  return sqlite3_exec(connection, "COMMIT;", nullptr, nullptr, nullptr) == SQLITE_OK;
}

void rollback_transaction(sqlite3* connection) {
  (void)sqlite3_exec(connection, "ROLLBACK;", nullptr, nullptr, nullptr);
}

[[nodiscard]] std::vector<std::string> load_all_session_ids(sqlite3* connection) {
  std::vector<std::string> session_ids;
  sqlite3_stmt* statement = nullptr;
  constexpr auto query = "SELECT session_id FROM sessions";
  if (sqlite3_prepare_v2(connection, query, -1, &statement, nullptr) != SQLITE_OK) {
    return session_ids;
  }

  while (sqlite3_step(statement) == SQLITE_ROW) {
    if (const auto session_id = store::sqlite::column_text(statement, 0);
        session_id.has_value()) {
      session_ids.push_back(*session_id);
    }
  }

  sqlite3_finalize(statement);
  return session_ids;
}

[[nodiscard]] std::vector<std::string> load_turn_ids_for_session(
    sqlite3* connection,
    const std::string& session_id) {
  std::vector<std::string> turn_ids;
  sqlite3_stmt* statement = nullptr;
  constexpr auto query =
      "SELECT turn_id FROM turns WHERE session_id = ?1 ORDER BY created_at ASC, turn_id ASC";
  if (sqlite3_prepare_v2(connection, query, -1, &statement, nullptr) != SQLITE_OK) {
    return turn_ids;
  }

  sqlite3_bind_text(statement, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
  while (sqlite3_step(statement) == SQLITE_ROW) {
    if (const auto turn_id = store::sqlite::column_text(statement, 0);
        turn_id.has_value()) {
      turn_ids.push_back(*turn_id);
    }
  }

  sqlite3_finalize(statement);
  return turn_ids;
}

[[nodiscard]] std::unordered_set<std::string> load_summary_source_turn_ids(
    sqlite3* connection,
    const std::string& session_id) {
  std::unordered_set<std::string> protected_turn_ids;
  sqlite3_stmt* statement = nullptr;
  constexpr auto query =
      "SELECT source_turn_ids_json FROM summaries WHERE session_id = ?1";
  if (sqlite3_prepare_v2(connection, query, -1, &statement, nullptr) != SQLITE_OK) {
    return protected_turn_ids;
  }

  sqlite3_bind_text(statement, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
  while (sqlite3_step(statement) == SQLITE_ROW) {
    const auto encoded_turn_ids = store::sqlite::column_text(statement, 0);
    if (!encoded_turn_ids.has_value()) {
      continue;
    }
    for (const auto& turn_id : store::sqlite::decode_string_array(*encoded_turn_ids)) {
      protected_turn_ids.insert(turn_id);
    }
  }

  sqlite3_finalize(statement);
  return protected_turn_ids;
}

[[nodiscard]] std::optional<std::int64_t> extract_valid_until(
    const std::optional<std::string>& encoded_validity_ref) {
  if (!encoded_validity_ref.has_value()) {
    return std::nullopt;
  }

  for (const auto& token : store::sqlite::decode_string_array(*encoded_validity_ref)) {
    constexpr std::string_view prefix = "valid_until:";
    if (token.rfind(prefix, 0) == 0) {
      return std::stoll(token.substr(prefix.size()));
    }
  }

  return std::nullopt;
}

[[nodiscard]] bool update_session_turn_index(sqlite3* connection,
                                             const std::string& session_id,
                                             const std::vector<std::string>& remaining_turn_ids) {
  sqlite3_stmt* statement = nullptr;
  constexpr auto update_sql =
      "UPDATE sessions SET turn_ids_json = ?1 WHERE session_id = ?2";
  if (sqlite3_prepare_v2(connection, update_sql, -1, &statement, nullptr) != SQLITE_OK) {
    return false;
  }

  const std::optional<std::vector<std::string>> encoded_turn_ids = remaining_turn_ids;
  sqlite3_bind_text(statement, 1,
                    store::sqlite::encode_string_array(encoded_turn_ids).c_str(), -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_text(statement, 2, session_id.c_str(), -1, SQLITE_TRANSIENT);

  const int sqlite_status = sqlite3_step(statement);
  sqlite3_finalize(statement);
  return sqlite_status == SQLITE_DONE;
}

[[nodiscard]] bool delete_turn_ids_for_session(sqlite3* connection,
                                               const std::string& session_id,
                                               const std::vector<std::string>& delete_turn_ids,
                                               const std::vector<std::string>& remaining_turn_ids,
                                               int retry_limit,
                                               MaintenanceReport& report) {
  for (int attempt = 0; attempt < retry_limit; ++attempt) {
    if (!begin_transaction(connection)) {
      if (attempt + 1 < retry_limit) {
        continue;
      }
      append_warning_once(report.warnings, "turn_retention_transaction_failed");
      return false;
    }

    sqlite3_stmt* delete_statement = nullptr;
    constexpr auto delete_sql =
        "DELETE FROM turns WHERE session_id = ?1 AND turn_id = ?2";
    if (sqlite3_prepare_v2(connection, delete_sql, -1, &delete_statement, nullptr) != SQLITE_OK) {
      rollback_transaction(connection);
      append_warning_once(report.warnings, "turn_retention_prepare_failed");
      return false;
    }

    bool failed = false;
    bool retryable = false;
    for (const auto& turn_id : delete_turn_ids) {
      sqlite3_reset(delete_statement);
      sqlite3_clear_bindings(delete_statement);
      sqlite3_bind_text(delete_statement, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(delete_statement, 2, turn_id.c_str(), -1, SQLITE_TRANSIENT);
      const int sqlite_status = sqlite3_step(delete_statement);
      if (sqlite_status != SQLITE_DONE) {
        failed = true;
        retryable = is_busy_code(sqlite_status);
        break;
      }
    }
    sqlite3_finalize(delete_statement);

    if (failed || !update_session_turn_index(connection, session_id, remaining_turn_ids)) {
      rollback_transaction(connection);
      if ((retryable || !failed) && attempt + 1 < retry_limit) {
        continue;
      }
      append_warning_once(report.warnings, "turn_retention_failed");
      return false;
    }

    if (!commit_transaction(connection)) {
      rollback_transaction(connection);
      if (attempt + 1 < retry_limit) {
        continue;
      }
      append_warning_once(report.warnings, "turn_retention_commit_failed");
      return false;
    }

    return true;
  }

  append_warning_once(report.warnings, "turn_retention_failed");
  return false;
}

[[nodiscard]] std::vector<std::string> collect_fact_ids_to_purge(
    sqlite3* connection,
    std::int64_t now_millis,
    std::int64_t fact_ttl_ms) {
  std::vector<std::string> fact_ids;
  sqlite3_stmt* statement = nullptr;
  constexpr auto query =
      "SELECT fact_id, created_at, validity_ref FROM facts WHERE superseded_by_fact_id IS NOT NULL";
  if (sqlite3_prepare_v2(connection, query, -1, &statement, nullptr) != SQLITE_OK) {
    return fact_ids;
  }

  while (sqlite3_step(statement) == SQLITE_ROW) {
    const auto fact_id = store::sqlite::column_text(statement, 0);
    if (!fact_id.has_value()) {
      continue;
    }

    const auto created_at = store::sqlite::column_int64(statement, 1).value_or(0);
    const auto valid_until = extract_valid_until(store::sqlite::column_text(statement, 2));
    const bool expired_by_valid_until = valid_until.has_value() && *valid_until <= now_millis;
    const bool expired_by_ttl = fact_ttl_ms > 0 && created_at > 0 &&
                                created_at + fact_ttl_ms <= now_millis;
    if (expired_by_valid_until || expired_by_ttl) {
      fact_ids.push_back(*fact_id);
    }
  }

  sqlite3_finalize(statement);
  return fact_ids;
}

[[nodiscard]] std::vector<std::string> collect_experience_ids_to_purge(
    sqlite3* connection,
    std::int64_t now_millis,
    std::int64_t experience_ttl_ms) {
  std::vector<std::string> experience_ids;
  sqlite3_stmt* statement = nullptr;
  constexpr auto query =
      "SELECT experience_id, created_at, expires_at FROM experiences "
      "WHERE superseded_by_experience_id IS NOT NULL";
  if (sqlite3_prepare_v2(connection, query, -1, &statement, nullptr) != SQLITE_OK) {
    return experience_ids;
  }

  while (sqlite3_step(statement) == SQLITE_ROW) {
    const auto experience_id = store::sqlite::column_text(statement, 0);
    if (!experience_id.has_value()) {
      continue;
    }

    const auto created_at = store::sqlite::column_int64(statement, 1).value_or(0);
    const auto expires_at = store::sqlite::column_int64(statement, 2);
    const bool expired_by_record = expires_at.has_value() && *expires_at <= now_millis;
    const bool expired_by_ttl = experience_ttl_ms > 0 && created_at > 0 &&
                                created_at + experience_ttl_ms <= now_millis;
    if (expired_by_record || expired_by_ttl) {
      experience_ids.push_back(*experience_id);
    }
  }

  sqlite3_finalize(statement);
  return experience_ids;
}

[[nodiscard]] bool delete_ids(sqlite3* connection,
                              const char* delete_sql,
                              const std::vector<std::string>& ids,
                              int retry_limit,
                              std::vector<std::string>& warnings,
                              const char* warning_key) {
  if (ids.empty()) {
    return true;
  }

  for (int attempt = 0; attempt < retry_limit; ++attempt) {
    if (!begin_transaction(connection)) {
      if (attempt + 1 < retry_limit) {
        continue;
      }
      append_warning_once(warnings, warning_key);
      return false;
    }

    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(connection, delete_sql, -1, &statement, nullptr) != SQLITE_OK) {
      rollback_transaction(connection);
      append_warning_once(warnings, warning_key);
      return false;
    }

    bool failed = false;
    bool retryable = false;
    for (const auto& id : ids) {
      sqlite3_reset(statement);
      sqlite3_clear_bindings(statement);
      sqlite3_bind_text(statement, 1, id.c_str(), -1, SQLITE_TRANSIENT);
      const int sqlite_status = sqlite3_step(statement);
      if (sqlite_status != SQLITE_DONE) {
        failed = true;
        retryable = is_busy_code(sqlite_status);
        break;
      }
    }

    sqlite3_finalize(statement);
    if (failed) {
      rollback_transaction(connection);
      if (retryable && attempt + 1 < retry_limit) {
        continue;
      }
      append_warning_once(warnings, warning_key);
      return false;
    }

    if (!commit_transaction(connection)) {
      rollback_transaction(connection);
      if (attempt + 1 < retry_limit) {
        continue;
      }
      append_warning_once(warnings, warning_key);
      return false;
    }

    return true;
  }

  append_warning_once(warnings, warning_key);
  return false;
}

[[nodiscard]] int cleanup_quarantine_records(sqlite3* connection,
                                             std::int64_t cutoff_millis,
                                             int retry_limit,
                                             std::vector<std::string>& warnings) {
  for (int attempt = 0; attempt < retry_limit; ++attempt) {
    if (!begin_transaction(connection)) {
      if (attempt + 1 < retry_limit) {
        continue;
      }
      append_warning_once(warnings, "quarantine_cleanup_failed");
      return 0;
    }

    sqlite3_stmt* statement = nullptr;
    constexpr auto delete_sql =
        "DELETE FROM quarantined_records WHERE created_at < ?1";
    if (sqlite3_prepare_v2(connection, delete_sql, -1, &statement, nullptr) != SQLITE_OK) {
      rollback_transaction(connection);
      append_warning_once(warnings, "quarantine_cleanup_failed");
      return 0;
    }

    sqlite3_bind_int64(statement, 1, cutoff_millis);
    const int sqlite_status = sqlite3_step(statement);
    const int cleaned = sqlite_status == SQLITE_DONE ? sqlite3_changes(connection) : 0;
    sqlite3_finalize(statement);

    if (sqlite_status != SQLITE_DONE) {
      rollback_transaction(connection);
      if (is_busy_code(sqlite_status) && attempt + 1 < retry_limit) {
        continue;
      }
      append_warning_once(warnings, "quarantine_cleanup_failed");
      return 0;
    }

    if (!commit_transaction(connection)) {
      rollback_transaction(connection);
      if (attempt + 1 < retry_limit) {
        continue;
      }
      append_warning_once(warnings, "quarantine_cleanup_failed");
      return 0;
    }

    return cleaned;
  }

  append_warning_once(warnings, "quarantine_cleanup_failed");
  return 0;
}

void run_wal_checkpoint(sqlite3* connection,
                        const MemoryConfig& config,
                        MaintenanceReport& report) {
  if (config.storage.journal_mode != JournalMode::Wal) {
    append_warning_once(report.warnings, "checkpoint_skipped_non_wal");
    return;
  }

  if (config.storage.checkpoint_mode != CheckpointMode::Passive) {
    append_warning_once(report.warnings, "checkpoint_mode_forced_passive");
  }

  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(connection, "PRAGMA wal_checkpoint(PASSIVE);", -1,
                         &statement, nullptr) != SQLITE_OK) {
    append_warning_once(report.warnings, "checkpoint_failed");
    return;
  }

  const int sqlite_status = sqlite3_step(statement);
  if (sqlite_status == SQLITE_ROW) {
    const int busy = sqlite3_column_int(statement, 0);
    const int wal_pages = sqlite3_column_int(statement, 1);
    const int checkpointed_pages = sqlite3_column_int(statement, 2);
    report.checkpoint_executed = true;
    report.checkpoint_wal_pages_remaining =
        std::max(0, wal_pages - checkpointed_pages);
    if (busy > 0) {
      append_warning_once(report.warnings, "checkpoint_busy");
    }
    if (config.storage.wal_autocheckpoint_pages > 0 &&
        report.checkpoint_wal_pages_remaining >
            config.storage.wal_autocheckpoint_pages * 3) {
      append_warning_once(report.warnings, "checkpoint_starvation");
    }
  } else {
    append_warning_once(report.warnings,
                        is_busy_code(sqlite_status) ? "checkpoint_busy"
                                                    : "checkpoint_failed");
  }

  sqlite3_finalize(statement);
}

[[nodiscard]] int run_turn_retention(sqlite3* connection,
                                     const MemoryConfig& config,
                                     MaintenanceReport& report) {
  if (config.maintenance.retention_turns <= 0) {
    return 0;
  }

  int purged_turns = 0;
  const int retry_limit = std::max(1, config.storage.writer_retry_count + 1);
  for (const auto& session_id : load_all_session_ids(connection)) {
    const auto ordered_turn_ids = load_turn_ids_for_session(connection, session_id);
    if (static_cast<int>(ordered_turn_ids.size()) <= config.maintenance.retention_turns) {
      continue;
    }

    const auto protected_turn_ids = load_summary_source_turn_ids(connection, session_id);
    std::vector<std::string> delete_turn_ids;
    int retained_count = static_cast<int>(ordered_turn_ids.size());
    for (const auto& turn_id : ordered_turn_ids) {
      if (retained_count <= config.maintenance.retention_turns) {
        break;
      }
      if (protected_turn_ids.find(turn_id) != protected_turn_ids.end()) {
        continue;
      }
      delete_turn_ids.push_back(turn_id);
      --retained_count;
    }

    if (delete_turn_ids.empty()) {
      continue;
    }

    const std::unordered_set<std::string> delete_turn_id_set(
        delete_turn_ids.begin(), delete_turn_ids.end());
    std::vector<std::string> remaining_turn_ids;
    remaining_turn_ids.reserve(ordered_turn_ids.size() - delete_turn_ids.size());
    for (const auto& turn_id : ordered_turn_ids) {
      if (delete_turn_id_set.find(turn_id) == delete_turn_id_set.end()) {
        remaining_turn_ids.push_back(turn_id);
      }
    }

    if (delete_turn_ids_for_session(connection, session_id, delete_turn_ids,
                                    remaining_turn_ids, retry_limit, report)) {
      purged_turns += static_cast<int>(delete_turn_ids.size());
    }
  }

  return purged_turns;
}

[[nodiscard]] int run_fact_retention(sqlite3* connection,
                                     const MemoryConfig& config,
                                     MaintenanceReport& report) {
  const auto fact_ids = collect_fact_ids_to_purge(
      connection, current_time_millis(), config.maintenance.fact_ttl_ms);
  const int retry_limit = std::max(1, config.storage.writer_retry_count + 1);
  if (!delete_ids(connection, "DELETE FROM facts WHERE fact_id = ?1", fact_ids,
                  retry_limit, report.warnings, "fact_retention_failed")) {
    return 0;
  }
  return static_cast<int>(fact_ids.size());
}

[[nodiscard]] int run_experience_retention(sqlite3* connection,
                                           const MemoryConfig& config,
                                           MaintenanceReport& report) {
  const auto experience_ids = collect_experience_ids_to_purge(
      connection, current_time_millis(), config.maintenance.experience_ttl_ms);
  const int retry_limit = std::max(1, config.storage.writer_retry_count + 1);
  if (!delete_ids(connection,
                  "DELETE FROM experiences WHERE experience_id = ?1",
                  experience_ids, retry_limit, report.warnings,
                  "experience_retention_failed")) {
    return 0;
  }
  return static_cast<int>(experience_ids.size());
}

[[nodiscard]] int run_quarantine_cleanup(sqlite3* connection,
                                         const MemoryConfig& config,
                                         MaintenanceReport& report) {
  if (!config.maintenance.quarantine_enabled ||
      config.maintenance.quarantine_ttl_ms <= 0) {
    return 0;
  }

  const auto cutoff_millis =
      current_time_millis() - config.maintenance.quarantine_ttl_ms;
  const int retry_limit = std::max(1, config.storage.writer_retry_count + 1);
  return cleanup_quarantine_records(connection, cutoff_millis, retry_limit,
                                    report.warnings);
}

void run_vector_rebuild(VectorMemoryIndexAdapter* vector_adapter,
                        MaintenanceReport& report) {
  if (vector_adapter == nullptr || !vector_adapter->is_available()) {
    append_warning_once(report.warnings, "vector_rebuild_skipped");
    return;
  }

  const auto rebuild_result = vector_adapter->rebuild_index();
  if (!rebuild_result.ok) {
    append_warning_once(report.warnings, "vector_rebuild_failed");
    return;
  }

  report.vector_rebuild_executed = true;
}

}  // namespace

MemoryMaintenanceWorker::MemoryMaintenanceWorker(
    IMemoryStore& store,
    MemoryConfig config,
    VectorMemoryIndexAdapter* vector_adapter,
    std::shared_ptr<std::mutex> writer_mutex)
    : store_(store),
      config_(std::move(config)),
      vector_adapter_(vector_adapter),
      writer_mutex_(std::move(writer_mutex)) {}

MemoryMaintenanceWorker::~MemoryMaintenanceWorker() {
  stop();
}

void MemoryMaintenanceWorker::start() {
  std::lock_guard<std::mutex> lock(schedule_mutex_);
  if (started_ || !config_.maintenance.auto_schedule) {
    return;
  }

  stopped_ = false;
  started_ = true;
  worker_thread_ = std::thread(&MemoryMaintenanceWorker::background_loop, this);
}

void MemoryMaintenanceWorker::stop() {
  {
    std::lock_guard<std::mutex> lock(schedule_mutex_);
    stopped_ = true;
  }
  schedule_cv_.notify_one();

  if (worker_thread_.joinable()) {
    worker_thread_.join();
  }

  std::lock_guard<std::mutex> lock(schedule_mutex_);
  started_ = false;
}

MaintenanceReport MemoryMaintenanceWorker::execute(
    const MaintenanceRequest& request) {
  std::unique_lock<std::mutex> writer_lock;
  if (writer_mutex_) {
    writer_lock = std::unique_lock<std::mutex>(*writer_mutex_);
  }

  MaintenanceReport report;
  const auto started_at = current_time_millis();
  auto* sqlite_store = dynamic_cast<store::sqlite::SqliteMemoryStore*>(&store_);
  if (sqlite_store == nullptr) {
    append_warning_once(report.warnings, "maintenance_store_backend_unavailable");
    report.duration_ms = current_time_millis() - started_at;
    return report;
  }

  sqlite3* connection = sqlite_store->writer_connection_for_maintenance();
  if (connection == nullptr) {
    append_warning_once(report.warnings, "maintenance_store_not_open");
    report.duration_ms = current_time_millis() - started_at;
    return report;
  }

  if (request.run_checkpoint) {
    run_wal_checkpoint(connection, config_, report);
  }

  if (request.run_retention) {
    report.turns_purged = run_turn_retention(connection, config_, report);
    report.facts_purged = run_fact_retention(connection, config_, report);
    report.experiences_purged = run_experience_retention(connection, config_, report);
  }

  if (request.run_quarantine_cleanup) {
    report.quarantine_cleaned = run_quarantine_cleanup(connection, config_, report);
  }

  if (request.run_vector_rebuild) {
    run_vector_rebuild(vector_adapter_, report);
  }

  report.duration_ms = current_time_millis() - started_at;
  return report;
}

void MemoryMaintenanceWorker::background_loop() {
  const auto interval =
      std::chrono::milliseconds(std::max(std::int64_t{1}, config_.maintenance.schedule_interval_ms));
  const MaintenanceRequest scheduled_request{};

  std::unique_lock<std::mutex> lock(schedule_mutex_);
  while (!stopped_) {
    if (schedule_cv_.wait_for(lock, interval, [this]() { return stopped_; })) {
      break;
    }

    lock.unlock();
    (void)execute(scheduled_request);
    lock.lock();
  }
}

}  // namespace dasall::memory