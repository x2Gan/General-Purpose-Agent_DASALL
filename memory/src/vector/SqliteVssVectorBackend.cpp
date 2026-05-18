#include "vector/SqliteVssVectorBackend.h"

#include <sqlite3.h>

#include <chrono>
#include <iomanip>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

#include "error/MemoryError.h"

namespace dasall::memory {
namespace {

[[nodiscard]] StoreResult storage_unavailable_result(
    std::string error_message) {
  return StoreResult::failure(
      map_memory_error(MemoryError::StorageUnavailable).result_code,
      std::move(error_message));
}

[[nodiscard]] StoreResult validation_failure_result(
    std::string error_message) {
  return StoreResult::failure(
      contracts::ResultCode::ValidationFieldMissing,
      std::move(error_message));
}

[[nodiscard]] std::string resolve_backend_type(const VectorConfig& config) {
  if (!config.enabled || config.backend_type != VectorBackend::SqliteVss) {
    return "none";
  }

  return std::string(to_string_view(config.backend_type));
}

constexpr const char* kVectorDocumentsTable = "memory_vector_documents";
constexpr const char* kVectorIndexTable = "memory_vector_index";

[[nodiscard]] std::int64_t driver_current_time_millis() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

[[nodiscard]] StoreResult sqlite_error_result(
    sqlite3* db,
    const std::string& prefix) {
  return storage_unavailable_result(
      prefix + ": " + (db == nullptr ? std::string{"sqlite handle unavailable"}
                                       : std::string{sqlite3_errmsg(db)}));
}

[[nodiscard]] StoreResult exec_sql(
    sqlite3* db,
    const std::string& sql,
    const std::string& label) {
  char* error_message = nullptr;
  const int sqlite_status =
      sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &error_message);
  if (sqlite_status == SQLITE_OK) {
    return StoreResult::success();
  }

  const std::string detail = error_message == nullptr ? std::string{sqlite3_errmsg(db)}
                                                      : std::string{error_message};
  if (error_message != nullptr) {
    sqlite3_free(error_message);
  }
  return storage_unavailable_result(label + ": " + detail);
}

[[nodiscard]] std::string encode_embedding_json(const std::vector<float>& embedding) {
  std::ostringstream builder;
  builder << '[';
  for (std::size_t index = 0; index < embedding.size(); ++index) {
    if (index != 0U) {
      builder << ',';
    }
    builder << std::setprecision(9) << embedding[index];
  }
  builder << ']';
  return builder.str();
}

class LoadableSqliteVssDriver final : public SqliteVssVectorBackend::Driver {
 public:
  explicit LoadableSqliteVssDriver(const VectorConfig& config)
      : vector0_path_(config.sqlite_vss_vector0_path),
        vss0_path_(config.sqlite_vss_vss0_path) {}

  [[nodiscard]] bool reports_available() const override {
    return !vector0_path_.empty() && !vss0_path_.empty();
  }

  [[nodiscard]] StoreResult initialize(sqlite3* db,
                                       int embedding_dimension) override {
    if (db == nullptr) {
      return storage_unavailable_result("sqlite-vss initialize requires sqlite handle");
    }
    if (!reports_available()) {
      return storage_unavailable_result("sqlite-vss extension assets are not configured");
    }

    if (embedding_dimension_ != 0 && embedding_dimension_ != embedding_dimension) {
      return validation_failure_result(
          "sqlite-vss loadable driver received a mismatched embedding dimension");
    }

    if (!extensions_loaded_) {
      if (const int enable_status = sqlite3_enable_load_extension(db, 1);
          enable_status != SQLITE_OK) {
        return sqlite_error_result(db, "enable sqlite extension loading failed");
      }

      char* error_message = nullptr;
      const int vector0_status =
          sqlite3_load_extension(db, vector0_path_.c_str(), nullptr, &error_message);
      if (vector0_status != SQLITE_OK) {
        const std::string detail = error_message == nullptr
                                       ? std::string{sqlite3_errmsg(db)}
                                       : std::string{error_message};
        if (error_message != nullptr) {
          sqlite3_free(error_message);
        }
        (void)sqlite3_enable_load_extension(db, 0);
        return storage_unavailable_result("load vector0 extension failed: " + detail);
      }

      const int vss0_status =
          sqlite3_load_extension(db, vss0_path_.c_str(), nullptr, &error_message);
      if (vss0_status != SQLITE_OK) {
        const std::string detail = error_message == nullptr
                                       ? std::string{sqlite3_errmsg(db)}
                                       : std::string{error_message};
        if (error_message != nullptr) {
          sqlite3_free(error_message);
        }
        (void)sqlite3_enable_load_extension(db, 0);
        return storage_unavailable_result("load vss0 extension failed: " + detail);
      }

      if (error_message != nullptr) {
        sqlite3_free(error_message);
      }
      (void)sqlite3_enable_load_extension(db, 0);
      extensions_loaded_ = true;
    }

    const auto docs_table_result = exec_sql(
        db,
        std::string("CREATE TABLE IF NOT EXISTS ") + kVectorDocumentsTable +
            "(rowid INTEGER PRIMARY KEY AUTOINCREMENT, doc_id TEXT NOT NULL UNIQUE, "
            "doc_type TEXT NOT NULL, text_snippet TEXT NOT NULL, embedding_json TEXT NOT NULL, "
            "updated_at INTEGER NOT NULL)",
        "create vector document sidecar table failed");
    if (!docs_table_result.ok) {
      return docs_table_result;
    }

    std::ostringstream create_vector_table_sql;
    create_vector_table_sql << "CREATE VIRTUAL TABLE IF NOT EXISTS " << kVectorIndexTable
                            << " USING vss0(embedding(" << embedding_dimension << "))";
    const auto index_table_result = exec_sql(
        db,
        create_vector_table_sql.str(),
        "create sqlite-vss virtual table failed");
    if (!index_table_result.ok) {
      return index_table_result;
    }

    embedding_dimension_ = embedding_dimension;
    return StoreResult::success("sqlite-vss-init");
  }

  [[nodiscard]] StoreResult upsert(
      sqlite3* db,
      const VectorDocument& document,
      const std::vector<float>& embedding) override {
    if (db == nullptr) {
      return storage_unavailable_result("sqlite-vss upsert requires sqlite handle");
    }

    const auto init_result = initialize(db, static_cast<int>(embedding.size()));
    if (!init_result.ok) {
      return init_result;
    }

    const auto begin_result = exec_sql(db, "BEGIN IMMEDIATE;", "begin vector upsert transaction failed");
    if (!begin_result.ok) {
      return begin_result;
    }

    sqlite3_int64 rowid = 0;
    bool existing_row = false;
    sqlite3_stmt* select_statement = nullptr;
    const std::string select_sql = std::string("SELECT rowid FROM ") + kVectorDocumentsTable +
                                   " WHERE doc_id = ?1";
    if (sqlite3_prepare_v2(db, select_sql.c_str(), -1, &select_statement, nullptr) !=
        SQLITE_OK) {
      (void)sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
      return sqlite_error_result(db, "prepare vector row lookup failed");
    }

    sqlite3_bind_text(select_statement, 1, document.doc_id.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(select_statement) == SQLITE_ROW) {
      rowid = sqlite3_column_int64(select_statement, 0);
      existing_row = true;
    }
    sqlite3_finalize(select_statement);

    const std::string embedding_json = encode_embedding_json(embedding);
    const auto now = driver_current_time_millis();

    if (existing_row) {
      sqlite3_stmt* update_statement = nullptr;
      const std::string update_sql = std::string("UPDATE ") + kVectorDocumentsTable +
                                     " SET doc_type = ?1, text_snippet = ?2, embedding_json = ?3, updated_at = ?4 WHERE rowid = ?5";
      if (sqlite3_prepare_v2(db, update_sql.c_str(), -1, &update_statement, nullptr) !=
          SQLITE_OK) {
        (void)sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
        return sqlite_error_result(db, "prepare vector document update failed");
      }

      sqlite3_bind_text(update_statement, 1, document.doc_type.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(update_statement, 2, document.text.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(update_statement, 3, embedding_json.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_int64(update_statement, 4, now);
      sqlite3_bind_int64(update_statement, 5, rowid);
      if (sqlite3_step(update_statement) != SQLITE_DONE) {
        sqlite3_finalize(update_statement);
        (void)sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
        return sqlite_error_result(db, "update vector document sidecar failed");
      }
      sqlite3_finalize(update_statement);

      const auto delete_result = exec_sql(
          db,
          std::string("DELETE FROM ") + kVectorIndexTable + " WHERE rowid = " +
              std::to_string(rowid),
          "delete previous sqlite-vss row failed");
      if (!delete_result.ok) {
        (void)sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
        return delete_result;
      }
    } else {
      sqlite3_stmt* insert_statement = nullptr;
      const std::string insert_sql = std::string("INSERT INTO ") + kVectorDocumentsTable +
                                     "(doc_id, doc_type, text_snippet, embedding_json, updated_at) VALUES(?1, ?2, ?3, ?4, ?5)";
      if (sqlite3_prepare_v2(db, insert_sql.c_str(), -1, &insert_statement, nullptr) !=
          SQLITE_OK) {
        (void)sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
        return sqlite_error_result(db, "prepare vector document insert failed");
      }

      sqlite3_bind_text(insert_statement, 1, document.doc_id.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(insert_statement, 2, document.doc_type.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(insert_statement, 3, document.text.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(insert_statement, 4, embedding_json.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_int64(insert_statement, 5, now);
      if (sqlite3_step(insert_statement) != SQLITE_DONE) {
        sqlite3_finalize(insert_statement);
        (void)sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
        return sqlite_error_result(db, "insert vector document sidecar failed");
      }
      sqlite3_finalize(insert_statement);
      rowid = sqlite3_last_insert_rowid(db);
    }

    sqlite3_stmt* index_insert_statement = nullptr;
    const std::string index_insert_sql = std::string("INSERT INTO ") + kVectorIndexTable +
                                         "(rowid, embedding) VALUES(?1, ?2)";
    if (sqlite3_prepare_v2(db, index_insert_sql.c_str(), -1, &index_insert_statement, nullptr) !=
        SQLITE_OK) {
      (void)sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
      return sqlite_error_result(db, "prepare sqlite-vss index insert failed");
    }

    sqlite3_bind_int64(index_insert_statement, 1, rowid);
    sqlite3_bind_text(index_insert_statement, 2, embedding_json.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(index_insert_statement) != SQLITE_DONE) {
      sqlite3_finalize(index_insert_statement);
      (void)sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
      return sqlite_error_result(db, "insert sqlite-vss index row failed");
    }
    sqlite3_finalize(index_insert_statement);

    const auto commit_result = exec_sql(db, "COMMIT;", "commit vector upsert transaction failed");
    if (!commit_result.ok) {
      (void)sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
      return commit_result;
    }

    return StoreResult::success(document.doc_id);
  }

  [[nodiscard]] std::vector<VectorHit> search(
      sqlite3* db,
      const std::vector<float>& query_embedding,
      int top_k) const override {
    if (db == nullptr || top_k <= 0 || !reports_available()) {
      return {};
    }

    sqlite3_stmt* statement = nullptr;
    const std::string search_sql = std::string("SELECT rowid, distance FROM ") +
                                   kVectorIndexTable +
                                   " WHERE vss_search(embedding, ?1) LIMIT ?2";
    if (sqlite3_prepare_v2(db, search_sql.c_str(), -1, &statement, nullptr) != SQLITE_OK) {
      return {};
    }

    const std::string query_embedding_json = encode_embedding_json(query_embedding);
    sqlite3_bind_text(statement, 1, query_embedding_json.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(statement, 2, top_k);

    std::vector<VectorHit> hits;
    while (sqlite3_step(statement) == SQLITE_ROW) {
      const auto rowid = sqlite3_column_int64(statement, 0);
      const auto distance = sqlite3_column_double(statement, 1);

      sqlite3_stmt* doc_statement = nullptr;
      const std::string doc_sql = std::string("SELECT doc_id, doc_type, text_snippet FROM ") +
                                  kVectorDocumentsTable + " WHERE rowid = ?1";
      if (sqlite3_prepare_v2(db, doc_sql.c_str(), -1, &doc_statement, nullptr) != SQLITE_OK) {
        continue;
      }
      sqlite3_bind_int64(doc_statement, 1, rowid);
      if (sqlite3_step(doc_statement) != SQLITE_ROW) {
        sqlite3_finalize(doc_statement);
        continue;
      }

      VectorHit hit;
      if (const auto* doc_id = sqlite3_column_text(doc_statement, 0); doc_id != nullptr) {
        hit.doc_id = reinterpret_cast<const char*>(doc_id);
      }
      if (const auto* doc_type = sqlite3_column_text(doc_statement, 1); doc_type != nullptr) {
        hit.doc_type = reinterpret_cast<const char*>(doc_type);
      }
      if (const auto* text_snippet = sqlite3_column_text(doc_statement, 2); text_snippet != nullptr) {
        hit.text_snippet = reinterpret_cast<const char*>(text_snippet);
      }
      hit.score = distance;
      hits.push_back(std::move(hit));
      sqlite3_finalize(doc_statement);
    }

    sqlite3_finalize(statement);
    return hits;
  }

  [[nodiscard]] int indexed_doc_count(sqlite3* db) const override {
    if (db == nullptr) {
      return 0;
    }

    sqlite3_stmt* statement = nullptr;
    const std::string count_sql = std::string("SELECT COUNT(*) FROM ") + kVectorDocumentsTable;
    if (sqlite3_prepare_v2(db, count_sql.c_str(), -1, &statement, nullptr) != SQLITE_OK) {
      return 0;
    }

    int count = 0;
    if (sqlite3_step(statement) == SQLITE_ROW) {
      count = sqlite3_column_int(statement, 0);
    }
    sqlite3_finalize(statement);
    return count;
  }

  [[nodiscard]] StoreResult rebuild_index(sqlite3* db) override {
    if (db == nullptr) {
      return storage_unavailable_result("sqlite-vss rebuild requires sqlite handle");
    }
    if (!reports_available()) {
      return storage_unavailable_result("sqlite-vss extension assets are not configured");
    }
    if (embedding_dimension_ <= 0) {
      return validation_failure_result("sqlite-vss rebuild requires initialized embedding dimension");
    }

    const auto init_result = initialize(db, embedding_dimension_);
    if (!init_result.ok) {
      return init_result;
    }

    const auto begin_result = exec_sql(db, "BEGIN IMMEDIATE;", "begin vector rebuild transaction failed");
    if (!begin_result.ok) {
      return begin_result;
    }

    const auto drop_result = exec_sql(
        db,
        std::string("DROP TABLE IF EXISTS ") + kVectorIndexTable,
        "drop sqlite-vss virtual table failed");
    if (!drop_result.ok) {
      (void)sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
      return drop_result;
    }

    std::ostringstream create_vector_table_sql;
    create_vector_table_sql << "CREATE VIRTUAL TABLE IF NOT EXISTS " << kVectorIndexTable
                            << " USING vss0(embedding(" << embedding_dimension_ << "))";
    const auto create_result = exec_sql(
        db,
        create_vector_table_sql.str(),
        "recreate sqlite-vss virtual table failed");
    if (!create_result.ok) {
      (void)sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
      return create_result;
    }

    const auto refill_result = exec_sql(
        db,
        std::string("INSERT INTO ") + kVectorIndexTable +
            "(rowid, embedding) SELECT rowid, embedding_json FROM " +
            kVectorDocumentsTable,
        "rebuild sqlite-vss index rows failed");
    if (!refill_result.ok) {
      (void)sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
      return refill_result;
    }

    const auto commit_result = exec_sql(db, "COMMIT;", "commit vector rebuild transaction failed");
    if (!commit_result.ok) {
      (void)sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
      return commit_result;
    }

    return StoreResult::success("sqlite-vss-rebuild");
  }

 private:
  std::string vector0_path_;
  std::string vss0_path_;
  int embedding_dimension_ = 0;
  bool extensions_loaded_ = false;
};

}  // namespace

SqliteVssVectorBackend::SqliteVssVectorBackend(
    const VectorConfig& config,
    sqlite3* db,
    IEmbeddingAdapter* embedding_adapter,
    std::unique_ptr<Driver> driver)
    : config_(config),
      db_(db),
      embedding_adapter_(embedding_adapter),
      driver_(driver ? std::move(driver)
                     : std::make_unique<LoadableSqliteVssDriver>(config)),
      health_{
          .available = config.enabled &&
                       config.backend_type == VectorBackend::SqliteVss &&
                       db != nullptr && driver_->reports_available(),
          .indexed_doc_count = 0,
          .last_rebuild_at = 0,
          .backend_type = resolve_backend_type(config),
      } {}

bool SqliteVssVectorBackend::is_available() const {
  return can_operate();
}

StoreResult SqliteVssVectorBackend::upsert(const VectorDocument& doc) {
  const auto validation = validate_document(doc);
  if (!validation.ok) {
    return validation;
  }

  if (!can_operate()) {
    return storage_unavailable_result(
        "sqlite-vss backend is unavailable for upsert");
  }

  const auto embedding = resolve_document_embedding(doc);
  if (!embedding.has_value()) {
    return validation_failure_result(
        "vector document requires a precomputed embedding or a working embedding adapter");
  }

  const auto init_result = ensure_initialized(embedding->size());
  if (!init_result.ok) {
    return init_result;
  }

  const auto result = driver_->upsert(db_, doc, *embedding);
  if (!result.ok) {
    mark_unavailable();
    return result;
  }

  refresh_indexed_doc_count();
  if (!result.persisted_id.has_value()) {
    return StoreResult::success(doc.doc_id);
  }

  return result;
}

std::vector<VectorHit> SqliteVssVectorBackend::search(
    const std::string& query_text,
    int top_k) const {
  if (!can_operate() || top_k <= 0) {
    return {};
  }

  const auto query_embedding = resolve_query_embedding(query_text);
  if (!query_embedding.has_value()) {
    return {};
  }

  if (!initialized_) {
    const auto init_result =
        const_cast<SqliteVssVectorBackend*>(this)->ensure_initialized(query_embedding->size());
    if (!init_result.ok) {
      return {};
    }
  }

  if (!embedding_dimension_.has_value() ||
      query_embedding->size() != *embedding_dimension_) {
    return {};
  }

  if (health_.indexed_doc_count <= 0) {
    return {};
  }

  return driver_->search(db_, *query_embedding, top_k);
}

VectorIndexHealth SqliteVssVectorBackend::health() const {
  return health_;
}

StoreResult SqliteVssVectorBackend::rebuild_index() {
  if (!can_operate()) {
    return storage_unavailable_result(
        "sqlite-vss backend is unavailable for rebuild");
  }

  std::size_t embedding_dimension = embedding_dimension_.value_or(0U);
  if (embedding_dimension == 0U && embedding_adapter_ != nullptr &&
      embedding_adapter_->dimension() > 0) {
    embedding_dimension =
        static_cast<std::size_t>(embedding_adapter_->dimension());
  }

  if (embedding_dimension == 0U) {
    return validation_failure_result(
        "sqlite-vss rebuild requires a known embedding dimension");
  }

  const auto init_result = ensure_initialized(embedding_dimension);
  if (!init_result.ok) {
    return init_result;
  }

  const auto result = driver_->rebuild_index(db_);
  if (!result.ok) {
    mark_unavailable();
    return result;
  }

  health_.last_rebuild_at = current_time_millis();
  refresh_indexed_doc_count();
  if (!result.persisted_id.has_value()) {
    return StoreResult::success("sqlite-vss-rebuild");
  }

  return result;
}

bool SqliteVssVectorBackend::can_operate() const {
  return config_.enabled &&
         config_.backend_type == VectorBackend::SqliteVss &&
         db_ != nullptr && driver_ != nullptr && health_.available;
}

StoreResult SqliteVssVectorBackend::ensure_initialized(
    std::size_t embedding_dimension) {
  if (!can_operate()) {
    return storage_unavailable_result(
        "sqlite-vss backend is unavailable for initialization");
  }

  if (!accept_embedding_dimension(embedding_dimension)) {
    return validation_failure_result(
        "sqlite-vss backend received an unexpected embedding dimension");
  }

  if (initialized_) {
    return StoreResult::success();
  }

  const auto result =
      driver_->initialize(db_, static_cast<int>(*embedding_dimension_));
  if (!result.ok) {
    mark_unavailable();
    return result;
  }

  initialized_ = true;
  refresh_indexed_doc_count();
  return StoreResult::success(result.persisted_id);
}

StoreResult SqliteVssVectorBackend::validate_document(
    const VectorDocument& document) const {
  if (document.doc_id.empty()) {
    return validation_failure_result(
        "vector document doc_id is required");
  }

  if (document.doc_type.empty()) {
    return validation_failure_result(
        "vector document doc_type is required");
  }

  if (document.text.empty() && document.embedding.empty()) {
    return validation_failure_result(
        "vector document requires text or embedding content");
  }

  return StoreResult::success();
}

std::optional<std::vector<float>>
SqliteVssVectorBackend::resolve_document_embedding(
    const VectorDocument& document) const {
  if (!document.embedding.empty()) {
    return document.embedding;
  }

  if (embedding_adapter_ == nullptr || document.text.empty()) {
    return std::nullopt;
  }

  auto embedding = embedding_adapter_->embed(document.text);
  if (embedding.empty()) {
    return std::nullopt;
  }

  return embedding;
}

std::optional<std::vector<float>>
SqliteVssVectorBackend::resolve_query_embedding(
    const std::string& query_text) const {
  if (embedding_adapter_ == nullptr || query_text.empty()) {
    return std::nullopt;
  }

  auto embedding = embedding_adapter_->embed(query_text);
  if (embedding.empty()) {
    return std::nullopt;
  }

  return embedding;
}

bool SqliteVssVectorBackend::accept_embedding_dimension(
    std::size_t embedding_dimension) {
  if (embedding_dimension == 0U) {
    return false;
  }

  if (!embedding_dimension_.has_value()) {
    embedding_dimension_ = embedding_dimension;
    return true;
  }

  return *embedding_dimension_ == embedding_dimension;
}

void SqliteVssVectorBackend::mark_unavailable() {
  health_.available = false;
  initialized_ = false;
}

void SqliteVssVectorBackend::refresh_indexed_doc_count() {
  if (db_ == nullptr || driver_ == nullptr) {
    health_.indexed_doc_count = 0;
    return;
  }

  health_.indexed_doc_count = driver_->indexed_doc_count(db_);
}

std::int64_t SqliteVssVectorBackend::current_time_millis() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

}  // namespace dasall::memory