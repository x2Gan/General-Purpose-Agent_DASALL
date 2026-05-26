#include "vector/DetachedVectorIndexFactory.h"

#include <sqlite3.h>

#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>

#include "vector/SimpleLocalEmbeddingAdapter.h"
#include "vector/SqliteVssVectorBackend.h"
#include "vector/UnavailableVectorMemoryIndexAdapter.h"

namespace dasall::memory {
namespace {

[[nodiscard]] bool local_embedding_fallback_enabled() {
  const char* value = std::getenv("DASALL_DETACHED_VECTOR_LOCAL_FALLBACK");
  if (value == nullptr) {
    return false;
  }

  const std::string text(value);
  return text == "1" || text == "true" || text == "TRUE" || text == "on" ||
         text == "yes";
}

[[nodiscard]] std::unique_ptr<IEmbeddingAdapter> create_embedding_adapter(
    const MemoryConfig& config) {
  if (!config.vector.enabled ||
      config.vector.backend_type == VectorBackend::None ||
      !local_embedding_fallback_enabled()) {
    return nullptr;
  }

  return std::make_unique<SimpleLocalEmbeddingAdapter>();
}

[[nodiscard]] bool open_database(const MemoryConfig& config,
                                 const std::filesystem::path& database_path,
                                 int open_flags,
                                 sqlite3** database) {
  if (database == nullptr) {
    return false;
  }

  *database = nullptr;
  if (sqlite3_open_v2(database_path.string().c_str(),
                      database,
                      open_flags | SQLITE_OPEN_NOMUTEX,
                      nullptr) != SQLITE_OK) {
    if (*database != nullptr) {
      sqlite3_close(*database);
      *database = nullptr;
    }
    return false;
  }

  sqlite3_busy_timeout(*database, config.storage.busy_timeout_ms);
  return true;
}

[[nodiscard]] bool exec_sql(sqlite3* database, const std::string& sql) {
  if (database == nullptr) {
    return false;
  }

  char* error_message = nullptr;
  const int status = sqlite3_exec(database, sql.c_str(), nullptr, nullptr,
                                  &error_message);
  if (error_message != nullptr) {
    sqlite3_free(error_message);
  }

  return status == SQLITE_OK;
}

class DetachedVectorIndexAdapter final : public VectorMemoryIndexAdapter {
 public:
  DetachedVectorIndexAdapter(sqlite3* database,
                             std::unique_ptr<IEmbeddingAdapter> embedding_adapter,
                             std::unique_ptr<VectorMemoryIndexAdapter> delegate)
      : database_(database),
        embedding_adapter_(std::move(embedding_adapter)),
        delegate_(std::move(delegate)) {}

  ~DetachedVectorIndexAdapter() override {
    if (database_ != nullptr) {
      sqlite3_close(database_);
      database_ = nullptr;
    }
  }

  [[nodiscard]] bool is_available() const override {
    return delegate_ != nullptr && delegate_->is_available();
  }

  [[nodiscard]] StoreResult upsert(const VectorDocument& doc) override {
    if (delegate_ == nullptr) {
      return StoreResult::failure(contracts::ResultCode::RuntimeRetryExhausted,
                                  "detached vector delegate is unavailable");
    }

    return delegate_->upsert(doc);
  }

  [[nodiscard]] std::vector<VectorHit> search(
      const std::string& query_text,
      int top_k) const override {
    if (delegate_ == nullptr) {
      return {};
    }

    return delegate_->search(query_text, top_k);
  }

  [[nodiscard]] VectorIndexHealth health() const override {
    if (delegate_ == nullptr) {
      return VectorIndexHealth{};
    }

    return delegate_->health();
  }

  [[nodiscard]] StoreResult rebuild_index() override {
    if (delegate_ == nullptr) {
      return StoreResult::failure(contracts::ResultCode::RuntimeRetryExhausted,
                                  "detached vector delegate is unavailable");
    }

    return delegate_->rebuild_index();
  }

 private:
  sqlite3* database_ = nullptr;
  std::unique_ptr<IEmbeddingAdapter> embedding_adapter_;
  std::unique_ptr<VectorMemoryIndexAdapter> delegate_;
};

}  // namespace

std::unique_ptr<VectorMemoryIndexAdapter> create_detached_vector_index_adapter(
    const MemoryConfig& config,
    const std::filesystem::path& database_path) {
  if (!config.vector.enabled ||
      config.vector.backend_type == VectorBackend::None) {
    return nullptr;
  }

  if (database_path.empty()) {
    return std::make_unique<UnavailableVectorMemoryIndexAdapter>(config.vector);
  }

  if (config.storage.sqlite_min_version > 0 &&
      sqlite3_libversion_number() < config.storage.sqlite_min_version) {
    return std::make_unique<UnavailableVectorMemoryIndexAdapter>(config.vector);
  }

  sqlite3* database = nullptr;
  if (!open_database(config,
                     database_path,
                     SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                     &database)) {
    return std::make_unique<UnavailableVectorMemoryIndexAdapter>(config.vector);
  }

  const bool configured =
      exec_sql(database,
               "PRAGMA journal_mode = " +
                   std::string(to_string_view(config.storage.journal_mode)) + ";") &&
      exec_sql(database,
               "PRAGMA synchronous = " +
                   std::string(to_string_view(config.storage.synchronous)) + ";") &&
      exec_sql(database, "PRAGMA foreign_keys = ON;");
  if (!configured) {
    sqlite3_close(database);
    return std::make_unique<UnavailableVectorMemoryIndexAdapter>(config.vector);
  }

  auto embedding_adapter = create_embedding_adapter(config);
  if (embedding_adapter == nullptr) {
    sqlite3_close(database);
    return std::make_unique<UnavailableVectorMemoryIndexAdapter>(config.vector);
  }

  auto delegate = std::make_unique<SqliteVssVectorBackend>(config.vector,
                                                           database,
                                                           embedding_adapter.get());
  return std::make_unique<DetachedVectorIndexAdapter>(database,
                                                      std::move(embedding_adapter),
                                                      std::move(delegate));
}

bool detached_vector_index_backend_available(
    const MemoryConfig& config,
    const std::filesystem::path& database_path) {
  if (!config.vector.enabled ||
      config.vector.backend_type == VectorBackend::None ||
      database_path.empty() || !std::filesystem::exists(database_path)) {
    return false;
  }

  if (config.storage.sqlite_min_version > 0 &&
      sqlite3_libversion_number() < config.storage.sqlite_min_version) {
    return false;
  }

  sqlite3* database = nullptr;
  if (!open_database(config, database_path, SQLITE_OPEN_READONLY, &database)) {
    return false;
  }

  struct DatabaseGuard {
    sqlite3* handle = nullptr;
    ~DatabaseGuard() {
      if (handle != nullptr) {
        sqlite3_close(handle);
      }
    }
  } database_guard{database};

  const SqliteVssVectorBackend backend(config.vector, database, nullptr);
  return backend.is_available();
}

bool detached_vector_local_query_encoder_available(
    const MemoryConfig&) {
  return local_embedding_fallback_enabled();
}

std::vector<float> encode_detached_vector_query_for_local_fallback(
    const MemoryConfig&,
    std::string_view query_text) {
  if (query_text.empty()) {
    return {};
  }

  if (!local_embedding_fallback_enabled()) {
    return {};
  }

  const SimpleLocalEmbeddingAdapter embedding_adapter;
  return embedding_adapter.embed(std::string(query_text));
}

std::vector<VectorHit> search_detached_vector_index_by_embedding(
    const MemoryConfig& config,
    const std::filesystem::path& database_path,
    const std::vector<float>& query_embedding,
    int top_k) {
  if (!config.vector.enabled ||
      config.vector.backend_type == VectorBackend::None ||
      database_path.empty() || !std::filesystem::exists(database_path) ||
      query_embedding.empty() || top_k <= 0) {
    return {};
  }

  if (config.storage.sqlite_min_version > 0 &&
      sqlite3_libversion_number() < config.storage.sqlite_min_version) {
    return {};
  }

  sqlite3* database = nullptr;
  if (!open_database(config, database_path, SQLITE_OPEN_READONLY, &database)) {
    return {};
  }

  struct DatabaseGuard {
    sqlite3* handle = nullptr;
    ~DatabaseGuard() {
      if (handle != nullptr) {
        sqlite3_close(handle);
      }
    }
  } database_guard{database};

  const SqliteVssVectorBackend backend(config.vector, database, nullptr);
  return backend.search_embedding(query_embedding, top_k);
}

}  // namespace dasall::memory