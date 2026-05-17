#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "sqlite3.h"

#include "support/TestAssertions.h"
#include "vector/IEmbeddingAdapter.h"
#include "vector/SqliteVssVectorBackend.h"

namespace {

class CountingEmbeddingAdapter final : public dasall::memory::IEmbeddingAdapter {
 public:
  [[nodiscard]] std::vector<float> embed(const std::string& text) const override {
    ++embed_call_count_;
    seen_inputs_.push_back(text);
    return {1.0F, 2.0F, 3.0F};
  }

  [[nodiscard]] int dimension() const override {
    return 3;
  }

  [[nodiscard]] int embed_call_count() const {
    return embed_call_count_;
  }

 private:
  mutable int embed_call_count_ = 0;
  mutable std::vector<std::string> seen_inputs_;
};

class FakeSqliteVssDriver final : public dasall::memory::SqliteVssVectorBackend::Driver {
 public:
  bool available = true;
  bool fail_initialize = false;
  bool fail_upsert = false;
  bool fail_rebuild = false;
  int initialize_call_count = 0;
  int upsert_call_count = 0;
  int rebuild_call_count = 0;
  int indexed_doc_count_value = 0;
  int last_init_dimension = 0;
  std::string last_doc_id;
  std::vector<float> last_upsert_embedding;
  mutable int search_call_count = 0;
  mutable std::vector<float> last_query_embedding;
  std::vector<dasall::memory::VectorHit> hits_to_return;

  [[nodiscard]] bool reports_available() const override {
    return available;
  }

  [[nodiscard]] dasall::memory::StoreResult initialize(
      sqlite3* db,
      int embedding_dimension) override {
    if (db == nullptr) {
      return dasall::memory::StoreResult::failure(
          dasall::contracts::ResultCode::RuntimeRetryExhausted,
          "sqlite handle is required");
    }

    ++initialize_call_count;
    last_init_dimension = embedding_dimension;
    if (fail_initialize) {
      return dasall::memory::StoreResult::failure(
          dasall::contracts::ResultCode::RuntimeRetryExhausted,
          "sqlite-vss init failed");
    }

    return dasall::memory::StoreResult::success("sqlite-vss-init");
  }

  [[nodiscard]] dasall::memory::StoreResult upsert(
      sqlite3* db,
      const dasall::memory::VectorDocument& document,
      const std::vector<float>& embedding) override {
    if (db == nullptr) {
      return dasall::memory::StoreResult::failure(
          dasall::contracts::ResultCode::RuntimeRetryExhausted,
          "sqlite handle is required");
    }

    ++upsert_call_count;
    last_doc_id = document.doc_id;
    last_upsert_embedding = embedding;
    if (fail_upsert) {
      return dasall::memory::StoreResult::failure(
          dasall::contracts::ResultCode::RuntimeRetryExhausted,
          "sqlite-vss upsert failed");
    }

    indexed_doc_count_value = 1;
    return dasall::memory::StoreResult::success(document.doc_id);
  }

  [[nodiscard]] std::vector<dasall::memory::VectorHit> search(
      sqlite3* db,
      const std::vector<float>& query_embedding,
      int top_k) const override {
    if (db == nullptr) {
      return {};
    }

    ++search_call_count;
    last_query_embedding = query_embedding;
    if (top_k <= 0) {
      return {};
    }

    return hits_to_return;
  }

  [[nodiscard]] int indexed_doc_count(sqlite3* db) const override {
    if (db == nullptr) {
      return 0;
    }

    return indexed_doc_count_value;
  }

  [[nodiscard]] dasall::memory::StoreResult rebuild_index(sqlite3* db) override {
    if (db == nullptr) {
      return dasall::memory::StoreResult::failure(
          dasall::contracts::ResultCode::RuntimeRetryExhausted,
          "sqlite handle is required");
    }

    ++rebuild_call_count;
    if (fail_rebuild) {
      return dasall::memory::StoreResult::failure(
          dasall::contracts::ResultCode::RuntimeRetryExhausted,
          "sqlite-vss rebuild failed");
    }

    return dasall::memory::StoreResult::success("sqlite-vss-rebuild");
  }
};

using SqliteHandle = std::unique_ptr<sqlite3, decltype(&sqlite3_close)>;

[[nodiscard]] SqliteHandle open_in_memory_database() {
  sqlite3* db = nullptr;
  if (sqlite3_open(":memory:", &db) != SQLITE_OK) {
    const auto error_message = db == nullptr ? std::string{"sqlite open failed"}
                                             : std::string{sqlite3_errmsg(db)};
    if (db != nullptr) {
      sqlite3_close(db);
    }
    throw std::runtime_error(error_message);
  }

  return SqliteHandle{db, &sqlite3_close};
}

void test_sqlite_vss_vector_backend_executes_happy_path() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto db = open_in_memory_database();

  dasall::memory::VectorConfig config;
  config.enabled = true;
  config.backend_type = dasall::memory::VectorBackend::SqliteVss;

  CountingEmbeddingAdapter embedding_adapter;
  auto driver = std::make_unique<FakeSqliteVssDriver>();
  auto* driver_ptr = driver.get();
  driver_ptr->hits_to_return = {dasall::memory::VectorHit{
      .doc_id = "fact-035-001",
      .doc_type = "fact",
      .score = 0.97F,
      .text_snippet = "remember this fact",
  }};
  dasall::memory::SqliteVssVectorBackend backend(
      config, db.get(), &embedding_adapter, std::move(driver));

  dasall::memory::VectorDocument document;
  document.doc_id = "fact-035-001";
  document.doc_type = "fact";
  document.text = "remember this fact";

  const auto upsert_result = backend.upsert(document);
  const auto hits = backend.search("remember", 3);
  const auto rebuild_result = backend.rebuild_index();
  const auto health = backend.health();

  assert_true(backend.is_available(),
              "sqlite-vss backend should stay available across the happy path");
  assert_true(upsert_result.ok,
              "sqlite-vss backend should persist a vector document on the happy path");
  assert_equal(1, driver_ptr->initialize_call_count,
               "sqlite-vss backend should initialize the driver once");
  assert_equal(3, driver_ptr->last_init_dimension,
               "sqlite-vss backend should initialize with the embedding dimension");
  assert_equal(1, driver_ptr->upsert_call_count,
               "sqlite-vss backend should call driver upsert exactly once");
  assert_equal(std::string{"fact-035-001"}, driver_ptr->last_doc_id,
               "sqlite-vss backend should preserve the document id for driver upsert");
  assert_true(driver_ptr->last_upsert_embedding.size() == 3U,
              "sqlite-vss backend should pass the resolved embedding to driver upsert");
  assert_equal(2, embedding_adapter.embed_call_count(),
               "sqlite-vss backend should use the embedding adapter for both upsert and search when no embedding is provided");
  assert_equal(1, driver_ptr->search_call_count,
               "sqlite-vss backend should delegate search after a successful upsert");
  assert_true(driver_ptr->last_query_embedding.size() == 3U,
              "sqlite-vss backend should embed query text before delegating search");
  assert_true(hits.size() == 1U && hits.front().doc_id == "fact-035-001",
              "sqlite-vss backend should surface driver hits on the happy path");
  assert_true(rebuild_result.ok,
              "sqlite-vss backend should rebuild the index on the happy path");
  assert_equal(1, driver_ptr->rebuild_call_count,
               "sqlite-vss backend should call driver rebuild once");
  assert_true(health.available,
              "sqlite-vss backend health should remain available on the happy path");
  assert_equal(std::string{"sqlite-vss"}, health.backend_type,
               "sqlite-vss backend health should preserve the backend type");
  assert_equal(1, health.indexed_doc_count,
               "sqlite-vss backend health should refresh indexed doc count after upsert");
  assert_true(health.last_rebuild_at > 0,
              "sqlite-vss backend health should record rebuild time after a successful rebuild");
}

void test_sqlite_vss_vector_backend_returns_empty_hits_when_query_embedding_is_unavailable() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto db = open_in_memory_database();

  dasall::memory::VectorConfig config;
  config.enabled = true;
  config.backend_type = dasall::memory::VectorBackend::SqliteVss;

  auto driver = std::make_unique<FakeSqliteVssDriver>();
  auto* driver_ptr = driver.get();
  dasall::memory::SqliteVssVectorBackend backend(
      config, db.get(), nullptr, std::move(driver));

  dasall::memory::VectorDocument document;
  document.doc_id = "summary-035-001";
  document.doc_type = "summary";
  document.embedding = {1.0F, 2.0F, 3.0F};

  const auto upsert_result = backend.upsert(document);
  const auto hits = backend.search("requires embedding adapter", 4);

  assert_true(upsert_result.ok,
              "sqlite-vss backend should accept a precomputed embedding without an embedding adapter");
  assert_true(hits.empty(),
              "sqlite-vss backend should return no hits when query embedding cannot be generated");
  assert_equal(0, driver_ptr->search_call_count,
               "sqlite-vss backend should not delegate search when query embedding generation is unavailable");
}

void test_sqlite_vss_vector_backend_propagates_upsert_failure() {
  using dasall::tests::support::assert_true;

  auto db = open_in_memory_database();

  dasall::memory::VectorConfig config;
  config.enabled = true;
  config.backend_type = dasall::memory::VectorBackend::SqliteVss;

  CountingEmbeddingAdapter embedding_adapter;
  auto driver = std::make_unique<FakeSqliteVssDriver>();
  auto* driver_ptr = driver.get();
  driver_ptr->fail_upsert = true;
  dasall::memory::SqliteVssVectorBackend backend(
      config, db.get(), &embedding_adapter, std::move(driver));

  dasall::memory::VectorDocument document;
  document.doc_id = "fact-035-upsert-failure";
  document.doc_type = "fact";
  document.text = "this upsert should fail";

  const auto result = backend.upsert(document);

  assert_true(!result.ok,
              "sqlite-vss backend should surface driver upsert failures");
  assert_true(result.result_code.has_value() &&
                  *result.result_code == dasall::contracts::ResultCode::RuntimeRetryExhausted,
              "sqlite-vss backend should map driver upsert failures to runtime retry exhausted");
  assert_true(!backend.is_available(),
              "sqlite-vss backend should mark itself unavailable after a driver upsert failure");
  assert_true(driver_ptr->upsert_call_count == 1,
              "sqlite-vss backend should reach driver upsert before propagating the failure");
}

void test_sqlite_vss_vector_backend_propagates_rebuild_failure() {
  using dasall::tests::support::assert_true;

  auto db = open_in_memory_database();

  dasall::memory::VectorConfig config;
  config.enabled = true;
  config.backend_type = dasall::memory::VectorBackend::SqliteVss;

  CountingEmbeddingAdapter embedding_adapter;
  auto driver = std::make_unique<FakeSqliteVssDriver>();
  auto* driver_ptr = driver.get();
  driver_ptr->fail_rebuild = true;
  dasall::memory::SqliteVssVectorBackend backend(
      config, db.get(), &embedding_adapter, std::move(driver));

  const auto result = backend.rebuild_index();

  assert_true(!result.ok,
              "sqlite-vss backend should surface driver rebuild failures");
  assert_true(result.result_code.has_value() &&
                  *result.result_code == dasall::contracts::ResultCode::RuntimeRetryExhausted,
              "sqlite-vss backend should map driver rebuild failures to runtime retry exhausted");
  assert_true(!backend.is_available(),
              "sqlite-vss backend should mark itself unavailable after a rebuild failure");
  assert_true(driver_ptr->initialize_call_count == 1 &&
                  driver_ptr->rebuild_call_count == 1,
              "sqlite-vss backend should initialize before invoking driver rebuild");
}

void test_sqlite_vss_vector_backend_fail_closes_when_extension_path_is_missing() {
  using dasall::tests::support::assert_true;

  auto db = open_in_memory_database();

  dasall::memory::VectorConfig config;
  config.enabled = true;
  config.backend_type = dasall::memory::VectorBackend::SqliteVss;
  config.sqlite_vss_vector0_path = "/tmp/dasall-missing-vector0.so";
  config.sqlite_vss_vss0_path = "/tmp/dasall-missing-vss0.so";

  CountingEmbeddingAdapter embedding_adapter;
  dasall::memory::SqliteVssVectorBackend backend(config, db.get(), &embedding_adapter);

  dasall::memory::VectorDocument document;
  document.doc_id = "fact-035-missing-extension";
  document.doc_type = "fact";
  document.text = "extension path should fail closed";

  const auto result = backend.upsert(document);

  assert_true(!result.ok,
              "sqlite-vss backend should reject upsert when configured extension paths are missing");
  assert_true(result.result_code.has_value() &&
                  *result.result_code == dasall::contracts::ResultCode::RuntimeRetryExhausted,
              "missing sqlite-vss extension paths should surface as storage unavailable runtime failures");
  assert_true(!backend.is_available(),
              "sqlite-vss backend should mark itself unavailable after extension loading fails");
}

void test_sqlite_vss_vector_backend_loads_real_extensions_when_assets_are_available() {
  using dasall::tests::support::assert_true;

  auto db = open_in_memory_database();

  const auto asset_root = std::filesystem::path(DASALL_REPO_ROOT) /
                          "third_party/.cache/sqlite-vss/v0.1.2/linux-x86_64";
  const auto vector0_path = asset_root / "vector0.so";
  const auto vss0_path = asset_root / "vss0.so";
  if (!std::filesystem::exists(vector0_path) || !std::filesystem::exists(vss0_path)) {
    throw std::runtime_error("sqlite-vss cached assets are missing for the positive-path test");
  }

  dasall::memory::VectorConfig config;
  config.enabled = true;
  config.backend_type = dasall::memory::VectorBackend::SqliteVss;
  config.sqlite_vss_vector0_path = vector0_path.string();
  config.sqlite_vss_vss0_path = vss0_path.string();

  CountingEmbeddingAdapter embedding_adapter;
  dasall::memory::SqliteVssVectorBackend backend(config, db.get(), &embedding_adapter);

  dasall::memory::VectorDocument document;
  document.doc_id = "fact-035-real-extension";
  document.doc_type = "fact";
  document.text = "remember sqlite vss real extension path";

  const auto upsert_result = backend.upsert(document);
  const auto hits = backend.search("remember sqlite vss real extension path", 3);
  const auto health = backend.health();

  assert_true(upsert_result.ok,
              "sqlite-vss backend should persist vector documents when real extension assets are available");
  assert_true(!hits.empty() && hits.front().doc_id == document.doc_id,
              "sqlite-vss backend should return the stored document on the real extension positive path");
  assert_true(health.available && health.indexed_doc_count >= 1,
              "sqlite-vss backend health should report an available indexed state on the real extension path");
}

}  // namespace

int main() {
  try {
    test_sqlite_vss_vector_backend_executes_happy_path();
    test_sqlite_vss_vector_backend_returns_empty_hits_when_query_embedding_is_unavailable();
    test_sqlite_vss_vector_backend_propagates_upsert_failure();
    test_sqlite_vss_vector_backend_propagates_rebuild_failure();
    test_sqlite_vss_vector_backend_fail_closes_when_extension_path_is_missing();
    test_sqlite_vss_vector_backend_loads_real_extensions_when_assets_are_available();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}