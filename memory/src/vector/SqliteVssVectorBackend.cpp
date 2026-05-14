#include "vector/SqliteVssVectorBackend.h"

#include <chrono>
#include <memory>
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

class MissingSqliteVssDriver final : public SqliteVssVectorBackend::Driver {
 public:
  [[nodiscard]] bool reports_available() const override {
    return false;
  }

  [[nodiscard]] StoreResult initialize(sqlite3* db,
                                       int embedding_dimension) override {
    (void)db;
    (void)embedding_dimension;
    return storage_unavailable_result(
        "sqlite-vss extension is not linked into this build");
  }

  [[nodiscard]] StoreResult upsert(
      sqlite3* db,
      const VectorDocument& document,
      const std::vector<float>& embedding) override {
    (void)db;
    (void)document;
    (void)embedding;
    return storage_unavailable_result(
        "sqlite-vss extension is not linked into this build");
  }

  [[nodiscard]] std::vector<VectorHit> search(
      sqlite3* db,
      const std::vector<float>& query_embedding,
      int top_k) const override {
    (void)db;
    (void)query_embedding;
    (void)top_k;
    return {};
  }

  [[nodiscard]] int indexed_doc_count(sqlite3* db) const override {
    (void)db;
    return 0;
  }

  [[nodiscard]] StoreResult rebuild_index(sqlite3* db) override {
    (void)db;
    return storage_unavailable_result(
        "sqlite-vss extension is not linked into this build");
  }
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
                     : std::make_unique<MissingSqliteVssDriver>()),
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
  if (!can_operate() || !initialized_ || top_k <= 0) {
    return {};
  }

  const auto query_embedding = resolve_query_embedding(query_text);
  if (!query_embedding.has_value() || !embedding_dimension_.has_value() ||
      query_embedding->size() != *embedding_dimension_) {
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