#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "vector/VectorMemoryIndexAdapter.h"

struct sqlite3;

namespace dasall::memory {

class SqliteVssVectorBackend final : public VectorMemoryIndexAdapter {
 public:
  class Driver {
   public:
    virtual ~Driver() = default;

    [[nodiscard]] virtual bool reports_available() const = 0;
    [[nodiscard]] virtual StoreResult initialize(sqlite3* db,
                                                 int embedding_dimension) = 0;
    [[nodiscard]] virtual StoreResult upsert(
        sqlite3* db,
        const VectorDocument& document,
        const std::vector<float>& embedding) = 0;
    [[nodiscard]] virtual std::vector<VectorHit> search(
        sqlite3* db,
        const std::vector<float>& query_embedding,
        int top_k) const = 0;
    [[nodiscard]] virtual int indexed_doc_count(sqlite3* db) const = 0;
    [[nodiscard]] virtual StoreResult rebuild_index(sqlite3* db) = 0;
  };

  explicit SqliteVssVectorBackend(
      const VectorConfig& config,
      sqlite3* db,
      IEmbeddingAdapter* embedding_adapter = nullptr,
      std::unique_ptr<Driver> driver = nullptr);

  [[nodiscard]] bool is_available() const override;
  [[nodiscard]] StoreResult upsert(const VectorDocument& doc) override;
  [[nodiscard]] std::vector<VectorHit> search(
      const std::string& query_text,
      int top_k) const override;
  [[nodiscard]] std::vector<VectorHit> search_embedding(
      const std::vector<float>& query_embedding,
      int top_k) const;
  [[nodiscard]] VectorIndexHealth health() const override;
  [[nodiscard]] StoreResult rebuild_index() override;

 private:
  [[nodiscard]] bool can_operate() const;
  [[nodiscard]] StoreResult ensure_initialized(std::size_t embedding_dimension);
  [[nodiscard]] StoreResult validate_document(const VectorDocument& document) const;
  [[nodiscard]] std::optional<std::vector<float>> resolve_document_embedding(
      const VectorDocument& document) const;
  [[nodiscard]] std::optional<std::vector<float>> resolve_query_embedding(
      const std::string& query_text) const;
  [[nodiscard]] bool accept_embedding_dimension(std::size_t embedding_dimension);
  void mark_unavailable();
  void refresh_indexed_doc_count();
    [[nodiscard]] static std::int64_t current_time_millis();

  VectorConfig config_{};
  sqlite3* db_ = nullptr;
  IEmbeddingAdapter* embedding_adapter_ = nullptr;
  std::unique_ptr<Driver> driver_;
  bool initialized_ = false;
  std::optional<std::size_t> embedding_dimension_;
  VectorIndexHealth health_{};
};

}  // namespace dasall::memory