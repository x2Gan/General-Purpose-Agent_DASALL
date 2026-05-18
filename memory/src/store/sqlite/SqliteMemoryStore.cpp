#include "store/sqlite/SqliteMemoryStore.h"

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

#include "error/MemoryError.h"
#include "store/sqlite/RowMappers.h"

namespace dasall::memory::store::sqlite {
namespace {

[[nodiscard]] std::optional<contracts::ResultCode> map_sqlite_result(int sqlite_status) {
  switch (sqlite_status) {
    case SQLITE_OK:
    case SQLITE_DONE:
    case SQLITE_ROW:
      return std::nullopt;
    case SQLITE_BUSY:
    case SQLITE_LOCKED:
      return map_memory_error(MemoryError::StorageBusy).result_code;
    case SQLITE_CANTOPEN:
    case SQLITE_IOERR:
    case SQLITE_FULL:
    case SQLITE_READONLY:
      return map_memory_error(MemoryError::StorageUnavailable).result_code;
    case SQLITE_MISUSE:
      return map_memory_error(MemoryError::ConfigInvalid).result_code;
    default:
      return map_memory_error(MemoryError::SchemaMismatch).result_code;
  }
}

[[nodiscard]] std::int64_t current_time_millis() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

[[nodiscard]] std::optional<contracts::ResultCode> exec_sql(sqlite3* connection,
                                                             const std::string& sql) {
  char* error_message = nullptr;
  const int sqlite_status =
      sqlite3_exec(connection, sql.c_str(), nullptr, nullptr, &error_message);
  if (error_message != nullptr) {
    sqlite3_free(error_message);
  }
  return map_sqlite_result(sqlite_status);
}

[[nodiscard]] std::optional<contracts::ResultCode> validate_sqlite_runtime_version(
    const MemoryConfig& config) {
  if (config.storage.sqlite_min_version <= 0) {
    return map_memory_error(MemoryError::ConfigInvalid).result_code;
  }

  if (sqlite3_libversion_number() < config.storage.sqlite_min_version) {
    return map_memory_error(MemoryError::ConfigInvalid).result_code;
  }

  return std::nullopt;
}

[[nodiscard]] std::int64_t query_turn_count(sqlite3* connection,
                                            const std::string& session_id) {
  if (connection == nullptr) {
    return 0;
  }

  sqlite3_stmt* statement = nullptr;
  constexpr auto query = "SELECT COUNT(*) FROM turns WHERE session_id = ?1";
  if (sqlite3_prepare_v2(connection, query, -1, &statement, nullptr) != SQLITE_OK) {
    return 0;
  }

  sqlite3_bind_text(statement, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
  std::int64_t total = 0;
  if (sqlite3_step(statement) == SQLITE_ROW) {
    total = sqlite3_column_int64(statement, 0);
  }
  sqlite3_finalize(statement);
  return total;
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
    if (const auto session_id = column_text(statement, 0); session_id.has_value()) {
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
    if (const auto turn_id = column_text(statement, 0); turn_id.has_value()) {
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
    const auto encoded_turn_ids = column_text(statement, 0);
    if (!encoded_turn_ids.has_value()) {
      continue;
    }
    for (const auto& turn_id : decode_string_array(*encoded_turn_ids)) {
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

  for (const auto& token : decode_string_array(*encoded_validity_ref)) {
    constexpr std::string_view prefix = "valid_until:";
    if (token.rfind(prefix, 0) == 0) {
      return std::stoll(token.substr(prefix.size()));
    }
  }

  return std::nullopt;
}

[[nodiscard]] bool update_session_turn_index(
    sqlite3* connection,
    const std::string& session_id,
    const std::vector<std::string>& remaining_turn_ids) {
  sqlite3_stmt* statement = nullptr;
  constexpr auto update_sql =
      "UPDATE sessions SET turn_ids_json = ?1 WHERE session_id = ?2";
  if (sqlite3_prepare_v2(connection, update_sql, -1, &statement, nullptr) != SQLITE_OK) {
    return false;
  }

  const std::optional<std::vector<std::string>> encoded_turn_ids = remaining_turn_ids;
  sqlite3_bind_text(statement, 1, encode_string_array(encoded_turn_ids).c_str(), -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_text(statement, 2, session_id.c_str(), -1, SQLITE_TRANSIENT);

  const int sqlite_status = sqlite3_step(statement);
  sqlite3_finalize(statement);
  return sqlite_status == SQLITE_DONE;
}

[[nodiscard]] bool delete_turn_ids_for_session(
    sqlite3* connection,
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
    if (sqlite3_prepare_v2(connection, delete_sql, -1, &delete_statement, nullptr) !=
        SQLITE_OK) {
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
    const auto fact_id = column_text(statement, 0);
    if (!fact_id.has_value()) {
      continue;
    }

    const auto created_at = column_int64(statement, 1).value_or(0);
    const auto valid_until = extract_valid_until(column_text(statement, 2));
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
    const auto experience_id = column_text(statement, 0);
    if (!experience_id.has_value()) {
      continue;
    }

    const auto created_at = column_int64(statement, 1).value_or(0);
    const auto expires_at = column_int64(statement, 2);
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

void bind_optional_text(sqlite3_stmt* statement,
                        int parameter_index,
                        const std::optional<std::string>& value) {
  if (value.has_value()) {
    sqlite3_bind_text(statement, parameter_index, value->c_str(), -1, SQLITE_TRANSIENT);
  } else {
    sqlite3_bind_null(statement, parameter_index);
  }
}

void bind_optional_int64(sqlite3_stmt* statement,
                         int parameter_index,
                         const std::optional<std::int64_t>& value) {
  if (value.has_value()) {
    sqlite3_bind_int64(statement, parameter_index, *value);
  } else {
    sqlite3_bind_null(statement, parameter_index);
  }
}

[[nodiscard]] std::optional<std::string> resolve_session_user_id(
    sqlite3* connection,
    const std::optional<std::string>& session_id) {
  if (connection == nullptr || !session_id.has_value()) {
    return std::nullopt;
  }

  sqlite3_stmt* statement = nullptr;
  constexpr auto query =
      "SELECT user_id FROM sessions WHERE session_id = ?1 LIMIT 1";
  if (sqlite3_prepare_v2(connection, query, -1, &statement, nullptr) != SQLITE_OK) {
    return std::nullopt;
  }

  sqlite3_bind_text(statement, 1, session_id->c_str(), -1, SQLITE_TRANSIENT);
  std::optional<std::string> user_id;
  if (sqlite3_step(statement) == SQLITE_ROW) {
    user_id = column_text(statement, 0);
  }
  sqlite3_finalize(statement);
  return user_id;
}

[[nodiscard]] bool matches_stage(const contracts::ExperienceMemory& experience,
                                 const std::string& stage) {
  if (!experience.tags.has_value()) {
    return false;
  }

  return std::any_of(experience.tags->begin(), experience.tags->end(),
                     [&stage](const std::string& tag) {
                       return tag == stage || tag == std::string{"stage:"} + stage;
                     });
}

[[nodiscard]] bool matches_any_domain(
    const std::optional<std::vector<std::string>>& experience_domains,
    const std::vector<std::string>& query_domains) {
  if (!experience_domains.has_value()) {
    return false;
  }

  return std::any_of(query_domains.begin(), query_domains.end(),
                     [&experience_domains](const std::string& query_domain) {
                       return std::find(experience_domains->begin(),
                                        experience_domains->end(),
                                        query_domain) != experience_domains->end();
                     });
}

class SqliteStoreTransaction final : public IStoreTransaction {
 public:
  SqliteStoreTransaction(sqlite3* connection,
                         std::optional<contracts::ResultCode> begin_error)
      : connection_(connection), begin_error_(begin_error) {
    active_ = !begin_error_.has_value();
  }

  ~SqliteStoreTransaction() override {
    if (active_) {
      rollback();
    }
  }

  [[nodiscard]] std::optional<contracts::ResultCode> commit() override {
    if (begin_error_.has_value()) {
      return begin_error_;
    }

    if (!active_) {
      return std::nullopt;
    }

    const auto commit_result = exec_sql(connection_, "COMMIT;");
    if (!commit_result.has_value()) {
      active_ = false;
    }
    return commit_result;
  }

  void rollback() noexcept override {
    if (!active_) {
      return;
    }

    active_ = false;
    (void)exec_sql(connection_, "ROLLBACK;");
  }

 private:
  sqlite3* connection_ = nullptr;
  std::optional<contracts::ResultCode> begin_error_;
  bool active_ = false;
};

}  // namespace

SqliteMemoryStore::SqliteMemoryStore() = default;

SqliteMemoryStore::~SqliteMemoryStore() {
  close();
}

std::optional<contracts::ResultCode> SqliteMemoryStore::open(
    const MemoryConfig& config) {
  close();

  if (config.storage.db_path.empty()) {
    return contracts::ResultCode::ValidationFieldMissing;
  }

  if (const auto version_result = validate_sqlite_runtime_version(config);
      version_result.has_value()) {
    return version_result;
  }

  if (sqlite3_open(config.storage.db_path.c_str(), &writer_connection_) != SQLITE_OK) {
    close();
    return contracts::ResultCode::RuntimeRetryExhausted;
  }

  const std::string journal_mode_str{to_string_view(config.storage.journal_mode)};
  const std::string synchronous_str{to_string_view(config.storage.synchronous)};

  sqlite3_busy_timeout(writer_connection_, config.storage.busy_timeout_ms);
  if (const auto journal_result = exec_sql(
          writer_connection_, "PRAGMA journal_mode = " + journal_mode_str + ";");
      journal_result.has_value()) {
    close();
    return journal_result;
  }
  if (const auto sync_result = exec_sql(
          writer_connection_, "PRAGMA synchronous = " + synchronous_str + ";");
      sync_result.has_value()) {
    close();
    return sync_result;
  }
  if (const auto foreign_keys_result = exec_sql(writer_connection_, "PRAGMA foreign_keys = ON;");
      foreign_keys_result.has_value()) {
    close();
    return foreign_keys_result;
  }
  if (const auto checkpoint_result = exec_sql(
          writer_connection_,
          "PRAGMA wal_autocheckpoint = " +
              std::to_string(config.storage.wal_autocheckpoint_pages) + ";");
      checkpoint_result.has_value()) {
    close();
    return checkpoint_result;
  }

  migrator_ = std::make_unique<SqliteSchemaMigrator>(config.storage.migrations_dir);
  try {
    if (const auto migration_result = migrator_->migrate(writer_connection_);
        migration_result.has_value()) {
      close();
      return migration_result;
    }
  } catch (const std::exception&) {
    close();
    return contracts::ResultCode::RuntimeRetryExhausted;
  }

  const int reader_count = std::max(1, config.storage.reader_pool_size);
  for (int index = 0; index < reader_count; ++index) {
    sqlite3* reader_connection = nullptr;
    if (sqlite3_open(config.storage.db_path.c_str(), &reader_connection) != SQLITE_OK) {
      if (reader_connection != nullptr) {
        sqlite3_close(reader_connection);
      }
      close();
      return contracts::ResultCode::RuntimeRetryExhausted;
    }

    sqlite3_busy_timeout(reader_connection, config.storage.busy_timeout_ms);
    if (const auto query_only_result = exec_sql(reader_connection, "PRAGMA query_only = ON;");
        query_only_result.has_value()) {
      sqlite3_close(reader_connection);
      close();
      return query_only_result;
    }
    if (const auto foreign_keys_result = exec_sql(reader_connection, "PRAGMA foreign_keys = ON;");
        foreign_keys_result.has_value()) {
      sqlite3_close(reader_connection);
      close();
      return foreign_keys_result;
    }

    reader_connections_.push_back(reader_connection);
  }

  config_ = config;
  return std::nullopt;
}

void SqliteMemoryStore::close() noexcept {
  for (auto*& reader_connection : reader_connections_) {
    if (reader_connection != nullptr) {
      sqlite3_close(reader_connection);
      reader_connection = nullptr;
    }
  }
  reader_connections_.clear();

  if (writer_connection_ != nullptr) {
    sqlite3_close(writer_connection_);
    writer_connection_ = nullptr;
  }

  migrator_.reset();
  config_.reset();
  next_reader_index_ = 0;
}

std::unique_ptr<IStoreTransaction> SqliteMemoryStore::begin_immediate() {
  if (writer_connection_ == nullptr) {
    return std::make_unique<SqliteStoreTransaction>(
        writer_connection_, contracts::ResultCode::RuntimeRetryExhausted);
  }

  const auto begin_result = exec_sql(writer_connection_, "BEGIN IMMEDIATE;");
  return std::make_unique<SqliteStoreTransaction>(writer_connection_, begin_result);
}

SessionLoadBundle SqliteMemoryStore::load_session_bundle(
    const SessionLoadRequest& request) const {
  SessionLoadBundle bundle;
  sqlite3* reader_connection = select_reader_connection();
  if (reader_connection == nullptr) {
    return bundle;
  }

  sqlite3_stmt* session_statement = nullptr;
  constexpr auto session_query =
      "SELECT session_id, user_id, latest_summary_memory_ref, metadata_digest, "
      "turn_ids_json, created_at, last_active_at, tags_json "
      "FROM sessions WHERE session_id = ?1 LIMIT 1";
  if (sqlite3_prepare_v2(reader_connection, session_query, -1, &session_statement, nullptr) == SQLITE_OK) {
    sqlite3_bind_text(session_statement, 1, request.session_id.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(session_statement) == SQLITE_ROW) {
      bundle.session = map_row_to_session(session_statement);
    }
  }
  sqlite3_finalize(session_statement);

  bundle.total_turn_count = static_cast<int>(query_turn_count(reader_connection, request.session_id));

  sqlite3_stmt* turns_statement = nullptr;
  constexpr auto turns_query =
      "SELECT turn_id, session_id, user_input, agent_response, tool_call_refs_json, "
      "observation_refs_json, summary_memory_ref, created_at, tags_json "
      "FROM turns WHERE session_id = ?1 ORDER BY created_at DESC LIMIT ?2";
  if (sqlite3_prepare_v2(reader_connection, turns_query, -1, &turns_statement, nullptr) == SQLITE_OK) {
    sqlite3_bind_text(turns_statement, 1, request.session_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(turns_statement, 2, request.recent_turn_limit <= 0 ? 1000000 : request.recent_turn_limit);
    while (sqlite3_step(turns_statement) == SQLITE_ROW) {
      bundle.recent_turns.push_back(map_row_to_turn(turns_statement));
    }
  }
  sqlite3_finalize(turns_statement);

  return bundle;
}

StoreResult SqliteMemoryStore::create_session(const contracts::Session& session) {
  if (writer_connection_ == nullptr) {
    return StoreResult::failure(contracts::ResultCode::RuntimeRetryExhausted,
                                std::string{"sqlite store is not open"});
  }

  const auto validation = contracts::validate_session_field_rules(session);
  if (!validation.ok) {
    return StoreResult::failure(contracts::ResultCode::ValidationFieldMissing,
                                std::string(validation.reason));
  }

  sqlite3_stmt* statement = nullptr;
  constexpr auto insert_sql =
      "INSERT INTO sessions(session_id, user_id, latest_summary_memory_ref, metadata_digest, "
      "turn_ids_json, created_at, last_active_at, tags_json) "
      "VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8)";
  if (sqlite3_prepare_v2(writer_connection_, insert_sql, -1, &statement, nullptr) != SQLITE_OK) {
    return StoreResult::failure(contracts::ResultCode::RuntimeRetryExhausted,
                                std::string{"failed to prepare session insert"});
  }

  bind_optional_text(statement, 1, session.session_id);
  bind_optional_text(statement, 2, session.user_id);
  bind_optional_text(statement, 3, session.latest_summary_memory_ref);
  bind_optional_text(statement, 4, session.metadata_digest);
  sqlite3_bind_text(statement, 5, encode_string_array(session.turn_ids).c_str(), -1,
                    SQLITE_TRANSIENT);
  bind_optional_int64(statement, 6, session.created_at);
  bind_optional_int64(statement, 7, session.last_active_at);
  sqlite3_bind_text(statement, 8, encode_string_array(session.tags).c_str(), -1,
                    SQLITE_TRANSIENT);

  const int step_status = sqlite3_step(statement);
  sqlite3_finalize(statement);
  if (const auto result = map_sqlite_result(step_status); result.has_value()) {
    return StoreResult::failure(*result, std::string{"failed to insert session"});
  }

  return StoreResult::success(session.session_id);
}

StoreResult SqliteMemoryStore::append_turn(const contracts::Turn& turn) {
  if (writer_connection_ == nullptr) {
    return StoreResult::failure(contracts::ResultCode::RuntimeRetryExhausted,
                                std::string{"sqlite store is not open"});
  }

  const auto validation = contracts::validate_turn_field_rules(turn);
  if (!validation.ok) {
    return StoreResult::failure(contracts::ResultCode::ValidationFieldMissing,
                                std::string(validation.reason));
  }

  sqlite3_stmt* statement = nullptr;
  constexpr auto insert_sql =
      "INSERT INTO turns(turn_id, session_id, user_input, agent_response, tool_call_refs_json, "
      "observation_refs_json, summary_memory_ref, created_at, tags_json) "
      "VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9)";
  if (sqlite3_prepare_v2(writer_connection_, insert_sql, -1, &statement, nullptr) != SQLITE_OK) {
    return StoreResult::failure(contracts::ResultCode::RuntimeRetryExhausted,
                                std::string{"failed to prepare turn insert"});
  }

  bind_optional_text(statement, 1, turn.turn_id);
  bind_optional_text(statement, 2, turn.session_id);
  bind_optional_text(statement, 3, turn.user_input);
  bind_optional_text(statement, 4, turn.agent_response);
  sqlite3_bind_text(statement, 5, encode_string_array(turn.tool_call_refs).c_str(), -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_text(statement, 6, encode_string_array(turn.observation_refs).c_str(), -1,
                    SQLITE_TRANSIENT);
  bind_optional_text(statement, 7, turn.summary_memory_ref);
  bind_optional_int64(statement, 8, turn.created_at);
  sqlite3_bind_text(statement, 9, encode_string_array(turn.tags).c_str(), -1,
                    SQLITE_TRANSIENT);

  const int step_status = sqlite3_step(statement);
  sqlite3_finalize(statement);
  if (const auto result = map_sqlite_result(step_status); result.has_value()) {
    return StoreResult::failure(*result, std::string{"failed to insert turn"});
  }

  sqlite3_stmt* session_statement = nullptr;
  constexpr auto select_turns_sql =
      "SELECT turn_ids_json FROM sessions WHERE session_id = ?1 LIMIT 1";
  std::vector<std::string> turn_ids;
  if (sqlite3_prepare_v2(writer_connection_, select_turns_sql, -1, &session_statement, nullptr) == SQLITE_OK) {
    bind_optional_text(session_statement, 1, turn.session_id);
    if (sqlite3_step(session_statement) == SQLITE_ROW) {
      turn_ids = decode_string_array(column_text(session_statement, 0).value_or("[]"));
    }
  }
  sqlite3_finalize(session_statement);

  if (turn.turn_id.has_value()) {
    turn_ids.push_back(*turn.turn_id);
  }

  sqlite3_stmt* update_statement = nullptr;
  constexpr auto update_sql =
      "UPDATE sessions SET turn_ids_json = ?1, last_active_at = ?2 WHERE session_id = ?3";
  if (sqlite3_prepare_v2(writer_connection_, update_sql, -1, &update_statement, nullptr) != SQLITE_OK) {
    return StoreResult::failure(contracts::ResultCode::RuntimeRetryExhausted,
                                std::string{"failed to prepare session turn update"});
  }

  const std::optional<std::vector<std::string>> turn_ids_optional = turn_ids;
  sqlite3_bind_text(update_statement, 1, encode_string_array(turn_ids_optional).c_str(), -1,
                    SQLITE_TRANSIENT);
  bind_optional_int64(update_statement, 2, turn.created_at);
  bind_optional_text(update_statement, 3, turn.session_id);

  const int update_status = sqlite3_step(update_statement);
  sqlite3_finalize(update_statement);
  if (const auto result = map_sqlite_result(update_status); result.has_value()) {
    return StoreResult::failure(*result,
                                std::string{"failed to update session turn index"});
  }

  return StoreResult::success(turn.turn_id);
}

StoreResult SqliteMemoryStore::update_session_active(
    const std::string& session_id,
    std::int64_t last_active_at) {
  if (writer_connection_ == nullptr) {
    return StoreResult::failure(contracts::ResultCode::RuntimeRetryExhausted,
                                std::string{"sqlite store is not open"});
  }

  sqlite3_stmt* statement = nullptr;
  constexpr auto update_sql =
      "UPDATE sessions SET last_active_at = ?1 WHERE session_id = ?2";
  if (sqlite3_prepare_v2(writer_connection_, update_sql, -1, &statement, nullptr) != SQLITE_OK) {
    return StoreResult::failure(contracts::ResultCode::RuntimeRetryExhausted,
                                std::string{"failed to prepare session activity update"});
  }

  sqlite3_bind_int64(statement, 1, last_active_at);
  sqlite3_bind_text(statement, 2, session_id.c_str(), -1, SQLITE_TRANSIENT);

  const int step_status = sqlite3_step(statement);
  sqlite3_finalize(statement);
  if (const auto result = map_sqlite_result(step_status); result.has_value()) {
    return StoreResult::failure(*result,
                                std::string{"failed to update session activity"});
  }

  if (sqlite3_changes(writer_connection_) == 0) {
    return StoreResult::failure(contracts::ResultCode::ValidationFieldMissing,
                                std::string{"session not found"});
  }

  return StoreResult::success(session_id);
}

StoreResult SqliteMemoryStore::upsert_summary(
    const contracts::SummaryMemory& summary) {
  if (writer_connection_ == nullptr) {
    return StoreResult::failure(contracts::ResultCode::RuntimeRetryExhausted,
                                std::string{"sqlite store is not open"});
  }

  const auto validation = contracts::validate_summary_memory_field_rules(summary);
  if (!validation.ok) {
    return StoreResult::failure(contracts::ResultCode::ValidationFieldMissing,
                                std::string(validation.reason));
  }

  sqlite3_stmt* statement = nullptr;
  constexpr auto upsert_sql =
      "INSERT OR REPLACE INTO summaries(summary_id, session_id, summary_text, source_turn_ids_json, "
      "decisions_made_json, confirmed_facts_json, tool_outcomes_json, created_at, tags_json) "
      "VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9)";
  if (sqlite3_prepare_v2(writer_connection_, upsert_sql, -1, &statement, nullptr) != SQLITE_OK) {
    return StoreResult::failure(contracts::ResultCode::RuntimeRetryExhausted,
                                std::string{"failed to prepare summary upsert"});
  }

  bind_optional_text(statement, 1, summary.summary_id);
  bind_optional_text(statement, 2, summary.session_id);
  bind_optional_text(statement, 3, summary.summary_text);
  sqlite3_bind_text(statement, 4, encode_string_array(summary.source_turn_ids).c_str(), -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_text(statement, 5, encode_string_array(summary.decisions_made).c_str(), -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_text(statement, 6, encode_string_array(summary.confirmed_facts).c_str(), -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_text(statement, 7, encode_string_array(summary.tool_outcomes).c_str(), -1,
                    SQLITE_TRANSIENT);
  bind_optional_int64(statement, 8, summary.created_at);
  sqlite3_bind_text(statement, 9, encode_string_array(summary.tags).c_str(), -1,
                    SQLITE_TRANSIENT);

  const int step_status = sqlite3_step(statement);
  sqlite3_finalize(statement);
  if (const auto result = map_sqlite_result(step_status); result.has_value()) {
    return StoreResult::failure(*result, std::string{"failed to upsert summary"});
  }

  if (summary.session_id.has_value() && summary.summary_id.has_value()) {
    sqlite3_stmt* update_statement = nullptr;
    constexpr auto update_sql =
        "UPDATE sessions SET latest_summary_memory_ref = ?1 WHERE session_id = ?2";
    if (sqlite3_prepare_v2(writer_connection_, update_sql, -1, &update_statement, nullptr) ==
        SQLITE_OK) {
      bind_optional_text(update_statement, 1, summary.summary_id);
      bind_optional_text(update_statement, 2, summary.session_id);
      (void)sqlite3_step(update_statement);
    }
    sqlite3_finalize(update_statement);
  }

  return StoreResult::success(summary.summary_id);
}

std::optional<contracts::SummaryMemory> SqliteMemoryStore::load_latest_summary(
    const std::string& session_id) const {
  sqlite3* reader_connection = select_reader_connection();
  if (reader_connection == nullptr) {
    return std::nullopt;
  }

  sqlite3_stmt* statement = nullptr;
  constexpr auto query =
      "SELECT summary_id, session_id, summary_text, source_turn_ids_json, decisions_made_json, "
      "confirmed_facts_json, tool_outcomes_json, created_at, tags_json "
      "FROM summaries WHERE session_id = ?1 ORDER BY created_at DESC LIMIT 1";
  if (sqlite3_prepare_v2(reader_connection, query, -1, &statement, nullptr) != SQLITE_OK) {
    return std::nullopt;
  }

  sqlite3_bind_text(statement, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
  std::optional<contracts::SummaryMemory> summary;
  if (sqlite3_step(statement) == SQLITE_ROW) {
    summary = map_row_to_summary(statement);
  }
  sqlite3_finalize(statement);
  return summary;
}

FactQueryResult SqliteMemoryStore::query_facts(const FactQuery& query) const {
  FactQueryResult result;
  sqlite3* reader_connection = select_reader_connection();
  if (reader_connection == nullptr) {
    return result;
  }

  std::string sql =
      "SELECT fact_id, session_id, user_id, fact_text, source_turn_ids_json, confidence_score, "
      "fact_type, validity_ref, evidence_digest, superseded_by_fact_id, created_at, tags_json "
      "FROM facts WHERE 1=1";
  int bind_index = 1;
  int session_idx = 0, user_idx = 0, type_idx = 0, conf_idx = 0;

  if (query.session_id.has_value()) {
    sql += " AND session_id = ?" + std::to_string(bind_index);
    session_idx = bind_index++;
  }
  if (query.user_id.has_value()) {
    sql += " AND user_id = ?" + std::to_string(bind_index);
    user_idx = bind_index++;
  }
  if (query.fact_type.has_value()) {
    sql += " AND fact_type = ?" + std::to_string(bind_index);
    type_idx = bind_index++;
  }
  if (query.min_confidence.has_value()) {
    sql += " AND confidence_score >= ?" + std::to_string(bind_index);
    conf_idx = bind_index++;
  }
  if (query.exclude_superseded) {
    sql += " AND superseded_by_fact_id IS NULL";
  }
  sql += " ORDER BY created_at DESC";
  if (query.limit > 0) {
    sql += " LIMIT " + std::to_string(query.limit);
  }

  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(reader_connection, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK) {
    return result;
  }

  if (session_idx > 0) {
    sqlite3_bind_text(statement, session_idx, query.session_id->c_str(), -1, SQLITE_TRANSIENT);
  }
  if (user_idx > 0) {
    sqlite3_bind_text(statement, user_idx, query.user_id->c_str(), -1, SQLITE_TRANSIENT);
  }
  if (type_idx > 0) {
    sqlite3_bind_text(statement, type_idx, query.fact_type->c_str(), -1, SQLITE_TRANSIENT);
  }
  if (conf_idx > 0) {
    sqlite3_bind_int64(statement, conf_idx, static_cast<std::int64_t>(*query.min_confidence));
  }

  while (sqlite3_step(statement) == SQLITE_ROW) {
    result.facts.push_back(map_row_to_fact(statement));
  }

  sqlite3_finalize(statement);
  result.total_count = static_cast<int>(result.facts.size());
  return result;
}

StoreResult SqliteMemoryStore::insert_fact(const contracts::MemoryFact& fact) {
  if (writer_connection_ == nullptr) {
    return StoreResult::failure(contracts::ResultCode::RuntimeRetryExhausted,
                                std::string{"sqlite store is not open"});
  }

  const auto validation = contracts::validate_memory_fact_field_rules(fact);
  if (!validation.ok) {
    return StoreResult::failure(contracts::ResultCode::ValidationFieldMissing,
                                std::string(validation.reason));
  }

  sqlite3_stmt* statement = nullptr;
  constexpr auto insert_sql =
      "INSERT INTO facts(fact_id, session_id, user_id, fact_text, source_turn_ids_json, "
      "confidence_score, fact_type, validity_ref, evidence_digest, superseded_by_fact_id, created_at, tags_json) "
      "VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12)";
  if (sqlite3_prepare_v2(writer_connection_, insert_sql, -1, &statement, nullptr) != SQLITE_OK) {
    return StoreResult::failure(contracts::ResultCode::RuntimeRetryExhausted,
                                std::string{"failed to prepare fact insert"});
  }

  bind_optional_text(statement, 1, fact.fact_id);
  bind_optional_text(statement, 2, fact.session_id);
  bind_optional_text(statement, 3, resolve_session_user_id(writer_connection_, fact.session_id));
  bind_optional_text(statement, 4, fact.fact_text);
  sqlite3_bind_text(statement, 5, encode_string_array(fact.source_turn_ids).c_str(), -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_int64(statement, 6,
                     static_cast<std::int64_t>(fact.confidence_score.value_or(0)));
  sqlite3_bind_text(statement, 7, fact.fact_type.value_or("").c_str(), -1,
                    SQLITE_TRANSIENT);
  bind_optional_text(statement, 8, encode_fact_validity_ref(fact));
  bind_optional_text(statement, 9, fact.evidence_digest);
  bind_optional_text(statement, 10, fact.superseded_by_fact_id);
  bind_optional_int64(statement, 11, fact.created_at);
  sqlite3_bind_text(statement, 12, encode_string_array(fact.tags).c_str(), -1,
                    SQLITE_TRANSIENT);

  const int step_status = sqlite3_step(statement);
  sqlite3_finalize(statement);
  if (const auto result_code = map_sqlite_result(step_status); result_code.has_value()) {
    return StoreResult::failure(*result_code, std::string{"failed to insert fact"});
  }

  return StoreResult::success(fact.fact_id);
}

StoreResult SqliteMemoryStore::supersede_fact(const std::string& old_fact_id,
                                              const std::string& new_fact_id) {
  if (writer_connection_ == nullptr) {
    return StoreResult::failure(contracts::ResultCode::RuntimeRetryExhausted,
                                std::string{"sqlite store is not open"});
  }

  sqlite3_stmt* statement = nullptr;
  constexpr auto update_sql =
      "UPDATE facts SET superseded_by_fact_id = ?1 WHERE fact_id = ?2";
  if (sqlite3_prepare_v2(writer_connection_, update_sql, -1, &statement, nullptr) != SQLITE_OK) {
    return StoreResult::failure(contracts::ResultCode::RuntimeRetryExhausted,
                                std::string{"failed to prepare fact supersede update"});
  }

  sqlite3_bind_text(statement, 1, new_fact_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(statement, 2, old_fact_id.c_str(), -1, SQLITE_TRANSIENT);
  const int step_status = sqlite3_step(statement);
  sqlite3_finalize(statement);
  if (const auto result_code = map_sqlite_result(step_status); result_code.has_value()) {
    return StoreResult::failure(*result_code,
                                std::string{"failed to update superseded fact"});
  }

  if (sqlite3_changes(writer_connection_) == 0) {
    return StoreResult::failure(contracts::ResultCode::ValidationFieldMissing,
                                std::string{"old fact not found"});
  }

  return StoreResult::success(old_fact_id);
}

ExperienceQueryResult SqliteMemoryStore::query_experiences(
    const ExperienceQuery& query) const {
  ExperienceQueryResult result;
  sqlite3* reader_connection = select_reader_connection();
  if (reader_connection == nullptr) {
    return result;
  }

  std::string sql =
      "SELECT experience_id, session_id, user_id, lesson_summary, trigger_condition, "
      "recommended_action, source_turn_ids_json, effectiveness_score, applicable_domains_json, "
      "risk_notes_json, expires_at, superseded_by_experience_id, created_at, tags_json "
      "FROM experiences WHERE 1=1";
  int bind_index = 1;
  int session_idx = 0, user_idx = 0, expired_idx = 0;

  if (query.session_id.has_value()) {
    sql += " AND session_id = ?" + std::to_string(bind_index);
    session_idx = bind_index++;
  }
  if (query.user_id.has_value()) {
    sql += " AND user_id = ?" + std::to_string(bind_index);
    user_idx = bind_index++;
  }
  if (query.exclude_expired) {
    sql += " AND (expires_at IS NULL OR expires_at > ?" + std::to_string(bind_index) + ")";
    expired_idx = bind_index++;
  }
  sql += " ORDER BY created_at DESC";
  if (query.limit > 0) {
    sql += " LIMIT " + std::to_string(query.limit);
  }

  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(reader_connection, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK) {
    return result;
  }

  if (session_idx > 0) {
    sqlite3_bind_text(statement, session_idx, query.session_id->c_str(), -1, SQLITE_TRANSIENT);
  }
  if (user_idx > 0) {
    sqlite3_bind_text(statement, user_idx, query.user_id->c_str(), -1, SQLITE_TRANSIENT);
  }
  if (expired_idx > 0) {
    sqlite3_bind_int64(statement, expired_idx, current_time_millis());
  }

  while (sqlite3_step(statement) == SQLITE_ROW) {
    const auto experience = map_row_to_experience(statement);

    if (query.stage.has_value() && !matches_stage(experience, *query.stage)) {
      continue;
    }

    if (query.applicable_domains.has_value() &&
        !matches_any_domain(experience.applicable_domains, *query.applicable_domains)) {
      continue;
    }

    result.experiences.push_back(experience);
  }

  sqlite3_finalize(statement);
  result.total_count = static_cast<int>(result.experiences.size());
  return result;
}

StoreResult SqliteMemoryStore::insert_experience(
    const contracts::ExperienceMemory& experience) {
  if (writer_connection_ == nullptr) {
    return StoreResult::failure(contracts::ResultCode::RuntimeRetryExhausted,
                                std::string{"sqlite store is not open"});
  }

  const auto validation = contracts::validate_experience_memory_field_rules(experience);
  if (!validation.ok) {
    return StoreResult::failure(contracts::ResultCode::ValidationFieldMissing,
                                std::string(validation.reason));
  }

  sqlite3_stmt* statement = nullptr;
  constexpr auto insert_sql =
      "INSERT INTO experiences(experience_id, session_id, user_id, lesson_summary, trigger_condition, "
      "recommended_action, source_turn_ids_json, effectiveness_score, applicable_domains_json, "
      "risk_notes_json, expires_at, superseded_by_experience_id, created_at, tags_json) "
      "VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14)";
  if (sqlite3_prepare_v2(writer_connection_, insert_sql, -1, &statement, nullptr) != SQLITE_OK) {
    return StoreResult::failure(contracts::ResultCode::RuntimeRetryExhausted,
                                std::string{"failed to prepare experience insert"});
  }

  bind_optional_text(statement, 1, experience.experience_id);
  bind_optional_text(statement, 2, experience.session_id);
  bind_optional_text(statement, 3,
                     resolve_session_user_id(writer_connection_, experience.session_id));
  bind_optional_text(statement, 4, experience.lesson_summary);
  bind_optional_text(statement, 5, experience.trigger_condition);
  bind_optional_text(statement, 6, experience.recommended_action);
  sqlite3_bind_text(statement, 7, encode_string_array(experience.source_turn_ids).c_str(), -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_int64(statement, 8,
                     static_cast<std::int64_t>(experience.effectiveness_score.value_or(0)));
  sqlite3_bind_text(statement, 9, encode_string_array(experience.applicable_domains).c_str(), -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_text(statement, 10, encode_experience_risk_notes_json(experience).c_str(), -1,
                    SQLITE_TRANSIENT);
  bind_optional_int64(statement, 11, experience.expires_at);
  bind_optional_text(statement, 12, experience.superseded_by_experience_id);
  bind_optional_int64(statement, 13, experience.created_at);
  sqlite3_bind_text(statement, 14, encode_string_array(experience.tags).c_str(), -1,
                    SQLITE_TRANSIENT);

  const int step_status = sqlite3_step(statement);
  sqlite3_finalize(statement);
  if (const auto result_code = map_sqlite_result(step_status); result_code.has_value()) {
    return StoreResult::failure(*result_code,
                                std::string{"failed to insert experience"});
  }

  return StoreResult::success(experience.experience_id);
}

std::int64_t SqliteMemoryStore::count_turns(const std::string& session_id) const {
  return query_turn_count(select_reader_connection(), session_id);
}

StoreResult SqliteMemoryStore::quarantine_record(const std::string& object_type,
                                                 const std::string& object_id,
                                                 const std::string& reason) {
  if (writer_connection_ == nullptr) {
    return StoreResult::failure(contracts::ResultCode::RuntimeRetryExhausted,
                                std::string{"sqlite store is not open"});
  }

  sqlite3_stmt* statement = nullptr;
  constexpr auto insert_sql =
      "INSERT INTO quarantined_records(object_type, object_id, reason, payload_digest, created_at) "
      "VALUES(?1, ?2, ?3, ?4, ?5)";
  if (sqlite3_prepare_v2(writer_connection_, insert_sql, -1, &statement, nullptr) != SQLITE_OK) {
    return StoreResult::failure(contracts::ResultCode::RuntimeRetryExhausted,
                                std::string{"failed to prepare quarantine insert"});
  }

  sqlite3_bind_text(statement, 1, object_type.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(statement, 2, object_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(statement, 3, reason.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_null(statement, 4);
  sqlite3_bind_int64(statement, 5, current_time_millis());

  const int step_status = sqlite3_step(statement);
  sqlite3_finalize(statement);
  if (const auto result_code = map_sqlite_result(step_status); result_code.has_value()) {
    return StoreResult::failure(*result_code,
                                std::string{"failed to insert quarantine record"});
  }

  return StoreResult::success(object_id);
}

void SqliteMemoryStore::run_wal_checkpoint(const MemoryConfig& config,
                                           MaintenanceReport& report) {
  if (writer_connection_ == nullptr) {
    append_warning_once(report.warnings, "maintenance_store_not_open");
    return;
  }

  if (config.storage.journal_mode != JournalMode::Wal) {
    append_warning_once(report.warnings, "checkpoint_skipped_non_wal");
    return;
  }

  if (config.storage.checkpoint_mode != CheckpointMode::Passive) {
    append_warning_once(report.warnings, "checkpoint_mode_forced_passive");
  }

  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(writer_connection_, "PRAGMA wal_checkpoint(PASSIVE);", -1,
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

int SqliteMemoryStore::run_turn_retention(const MemoryConfig& config,
                                          MaintenanceReport& report) {
  if (writer_connection_ == nullptr) {
    append_warning_once(report.warnings, "maintenance_store_not_open");
    return 0;
  }

  if (config.maintenance.retention_turns <= 0) {
    return 0;
  }

  int purged_turns = 0;
  const int retry_limit = std::max(1, config.storage.writer_retry_count + 1);
  for (const auto& session_id : load_all_session_ids(writer_connection_)) {
    const auto ordered_turn_ids = load_turn_ids_for_session(writer_connection_, session_id);
    if (static_cast<int>(ordered_turn_ids.size()) <= config.maintenance.retention_turns) {
      continue;
    }

    const auto protected_turn_ids =
        load_summary_source_turn_ids(writer_connection_, session_id);
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

    if (delete_turn_ids_for_session(writer_connection_, session_id, delete_turn_ids,
                                    remaining_turn_ids, retry_limit, report)) {
      purged_turns += static_cast<int>(delete_turn_ids.size());
    }
  }

  return purged_turns;
}

int SqliteMemoryStore::run_fact_retention(const MemoryConfig& config,
                                          MaintenanceReport& report) {
  if (writer_connection_ == nullptr) {
    append_warning_once(report.warnings, "maintenance_store_not_open");
    return 0;
  }

  const auto fact_ids = collect_fact_ids_to_purge(
      writer_connection_, current_time_millis(), config.maintenance.fact_ttl_ms);
  const int retry_limit = std::max(1, config.storage.writer_retry_count + 1);
  if (!delete_ids(writer_connection_, "DELETE FROM facts WHERE fact_id = ?1", fact_ids,
                  retry_limit, report.warnings, "fact_retention_failed")) {
    return 0;
  }
  return static_cast<int>(fact_ids.size());
}

int SqliteMemoryStore::run_experience_retention(const MemoryConfig& config,
                                                MaintenanceReport& report) {
  if (writer_connection_ == nullptr) {
    append_warning_once(report.warnings, "maintenance_store_not_open");
    return 0;
  }

  const auto experience_ids = collect_experience_ids_to_purge(
      writer_connection_, current_time_millis(), config.maintenance.experience_ttl_ms);
  const int retry_limit = std::max(1, config.storage.writer_retry_count + 1);
  if (!delete_ids(writer_connection_,
                  "DELETE FROM experiences WHERE experience_id = ?1",
                  experience_ids, retry_limit, report.warnings,
                  "experience_retention_failed")) {
    return 0;
  }
  return static_cast<int>(experience_ids.size());
}

int SqliteMemoryStore::run_quarantine_cleanup(const MemoryConfig& config,
                                              MaintenanceReport& report) {
  if (writer_connection_ == nullptr) {
    append_warning_once(report.warnings, "maintenance_store_not_open");
    return 0;
  }

  if (!config.maintenance.quarantine_enabled ||
      config.maintenance.quarantine_ttl_ms <= 0) {
    return 0;
  }

  const auto cutoff_millis =
      current_time_millis() - config.maintenance.quarantine_ttl_ms;
  const int retry_limit = std::max(1, config.storage.writer_retry_count + 1);
  return cleanup_quarantine_records(writer_connection_, cutoff_millis, retry_limit,
                                    report.warnings);
}

sqlite3* SqliteMemoryStore::writer_connection_for_maintenance() {
  return writer_connection_;
}

sqlite3* SqliteMemoryStore::select_reader_connection() const {
  if (!reader_connections_.empty()) {
    sqlite3* connection = reader_connections_[next_reader_index_ % reader_connections_.size()];
    ++next_reader_index_;
    return connection;
  }

  return writer_connection_;
}

std::unique_ptr<IMemoryStore> create_sqlite_memory_store() {
  return std::make_unique<SqliteMemoryStore>();
}

}  // namespace dasall::memory::store::sqlite
