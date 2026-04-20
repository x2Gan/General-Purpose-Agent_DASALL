#include "store/sqlite/SqliteMemoryStore.h"

#include <sqlite3.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

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
      return contracts::ResultCode::RuntimeRetryExhausted;
    default:
      return contracts::ResultCode::ValidationFieldMissing;
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

  void rollback() override {
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

  if (sqlite3_open(config.storage.db_path.c_str(), &writer_connection_) != SQLITE_OK) {
    close();
    return contracts::ResultCode::RuntimeRetryExhausted;
  }

  sqlite3_busy_timeout(writer_connection_, config.storage.busy_timeout_ms);
  if (const auto journal_result = exec_sql(
          writer_connection_, "PRAGMA journal_mode = " + config.storage.journal_mode + ";");
      journal_result.has_value()) {
    close();
    return journal_result;
  }
  if (const auto sync_result = exec_sql(
          writer_connection_, "PRAGMA synchronous = " + config.storage.synchronous + ";");
      sync_result.has_value()) {
    close();
    return sync_result;
  }
  if (const auto foreign_keys_result = exec_sql(writer_connection_, "PRAGMA foreign_keys = ON;");
      foreign_keys_result.has_value()) {
    close();
    return foreign_keys_result;
  }

  migrator_ = std::make_unique<SqliteSchemaMigrator>(config.storage.migrations_dir);
  if (const auto migration_result = migrator_->migrate(writer_connection_);
      migration_result.has_value()) {
    close();
    return migration_result;
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

void SqliteMemoryStore::close() {
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
    const SessionLoadRequest& request) {
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
    const std::string& session_id) {
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

FactQueryResult SqliteMemoryStore::query_facts(const FactQuery& query) {
  FactQueryResult result;
  sqlite3* reader_connection = select_reader_connection();
  if (reader_connection == nullptr) {
    return result;
  }

  sqlite3_stmt* statement = nullptr;
  constexpr auto select_sql =
      "SELECT fact_id, session_id, user_id, fact_text, source_turn_ids_json, confidence_score, "
      "fact_type, validity_ref, evidence_digest, superseded_by_fact_id, created_at, tags_json "
      "FROM facts ORDER BY created_at DESC";
  if (sqlite3_prepare_v2(reader_connection, select_sql, -1, &statement, nullptr) != SQLITE_OK) {
    return result;
  }

  while (sqlite3_step(statement) == SQLITE_ROW) {
    const auto fact = map_row_to_fact(statement);

    if (query.session_id.has_value() && fact.session_id != query.session_id) {
      continue;
    }

    if (query.user_id.has_value() && column_text(statement, 2) != query.user_id) {
      continue;
    }

    if (query.fact_type.has_value() && fact.fact_type != query.fact_type) {
      continue;
    }

    if (query.min_confidence.has_value()) {
      if (!fact.confidence_score.has_value() ||
          static_cast<int>(*fact.confidence_score) < *query.min_confidence) {
        continue;
      }
    }

    if (query.exclude_superseded && fact.superseded_by_fact_id.has_value()) {
      continue;
    }

    result.facts.push_back(fact);
    if (query.limit > 0 && static_cast<int>(result.facts.size()) >= query.limit) {
      break;
    }
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
    const ExperienceQuery& query) {
  ExperienceQueryResult result;
  sqlite3* reader_connection = select_reader_connection();
  if (reader_connection == nullptr) {
    return result;
  }

  sqlite3_stmt* statement = nullptr;
  constexpr auto select_sql =
      "SELECT experience_id, session_id, user_id, lesson_summary, trigger_condition, "
      "recommended_action, source_turn_ids_json, effectiveness_score, applicable_domains_json, "
      "risk_notes_json, expires_at, superseded_by_experience_id, created_at, tags_json "
      "FROM experiences ORDER BY created_at DESC";
  if (sqlite3_prepare_v2(reader_connection, select_sql, -1, &statement, nullptr) != SQLITE_OK) {
    return result;
  }

  const auto now_millis = current_time_millis();
  while (sqlite3_step(statement) == SQLITE_ROW) {
    const auto experience = map_row_to_experience(statement);

    if (query.session_id.has_value() && experience.session_id != query.session_id) {
      continue;
    }

    if (query.user_id.has_value() && column_text(statement, 2) != query.user_id) {
      continue;
    }

    if (query.stage.has_value() && !matches_stage(experience, *query.stage)) {
      continue;
    }

    if (query.applicable_domains.has_value() &&
        !matches_any_domain(experience.applicable_domains, *query.applicable_domains)) {
      continue;
    }

    if (query.exclude_expired && experience.expires_at.has_value() &&
        *experience.expires_at <= now_millis) {
      continue;
    }

    result.experiences.push_back(experience);
    if (query.limit > 0 &&
        static_cast<int>(result.experiences.size()) >= query.limit) {
      break;
    }
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

std::int64_t SqliteMemoryStore::count_turns(const std::string& session_id) {
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

sqlite3* SqliteMemoryStore::select_reader_connection() {
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
