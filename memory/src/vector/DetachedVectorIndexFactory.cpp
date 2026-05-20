#include "vector/DetachedVectorIndexFactory.h"

#include <sqlite3.h>

#include <memory>
#include <optional>
#include <string>

#include "vector/SimpleLocalEmbeddingAdapter.h"
#include "vector/SqliteVssVectorBackend.h"
#include "vector/UnavailableVectorMemoryIndexAdapter.h"

namespace dasall::memory {
namespace {

[[nodiscard]] std::unique_ptr<IEmbeddingAdapter> create_embedding_adapter(
    const MemoryConfig& config) {
  if (!config.vector.enabled ||
      config.vector.backend_type == VectorBackend::None) {
    return nullptr;
  }

  return std::make_unique<SimpleLocalEmbeddingAdapter>();
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
  if (sqlite3_open_v2(database_path.string().c_str(),
                      &database,
                      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                          SQLITE_OPEN_NOMUTEX,
                      nullptr) != SQLITE_OK) {
    if (database != nullptr) {
      sqlite3_close(database);
    }
    return std::make_unique<UnavailableVectorMemoryIndexAdapter>(config.vector);
  }

  sqlite3_busy_timeout(database, config.storage.busy_timeout_ms);
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
  auto delegate = std::make_unique<SqliteVssVectorBackend>(config.vector,
                                                           database,
                                                           embedding_adapter.get());
  return std::make_unique<DetachedVectorIndexAdapter>(database,
                                                      std::move(embedding_adapter),
                                                      std::move(delegate));
}

}  // namespace dasall::memory