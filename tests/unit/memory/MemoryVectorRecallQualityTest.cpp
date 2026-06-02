#include <algorithm>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "sqlite3.h"

#include "LLMBackedEmbeddingAdapter.h"
#include "ILLMTransport.h"
#include "support/TestAssertions.h"
#include "vector/SimpleLocalEmbeddingAdapter.h"
#include "vector/SqliteVssVectorBackend.h"

namespace {

struct RecallFixture {
  std::string query_text;
  std::string relevant_doc_text;
  std::string distractor_doc_text;
};

[[nodiscard]] float dot_product(const std::vector<float>& left,
                                const std::vector<float>& right) {
  if (left.size() != right.size()) {
    return 0.0F;
  }

  float value = 0.0F;
  for (std::size_t index = 0U; index < left.size(); ++index) {
    value += left[index] * right[index];
  }
  return value;
}

[[nodiscard]] RecallFixture select_baseline_miss_fixture() {
  const std::vector<std::string> relevant_candidates = {
      "espresso",
      "cappuccino",
      "arabica",
      "macchiato",
  };
  const std::vector<std::string> distractor_candidates = {
      "sencha",
      "oolong",
      "matcha",
      "earlgrey",
  };
  const std::vector<std::string> query_candidates = {
      "latte",
      "americano",
      "flatwhite",
      "ristretto",
  };

  const dasall::memory::SimpleLocalEmbeddingAdapter baseline_adapter;
  for (const auto& query_text : query_candidates) {
    const auto query_embedding = baseline_adapter.embed(query_text);
    if (query_embedding.empty()) {
      continue;
    }

    for (const auto& relevant_doc_text : relevant_candidates) {
      const auto relevant_embedding = baseline_adapter.embed(relevant_doc_text);
      if (relevant_embedding.empty()) {
        continue;
      }
      const float relevant_score =
          dot_product(query_embedding, relevant_embedding);

      for (const auto& distractor_doc_text : distractor_candidates) {
        const auto distractor_embedding = baseline_adapter.embed(distractor_doc_text);
        if (distractor_embedding.empty()) {
          continue;
        }

        const float distractor_score =
            dot_product(query_embedding, distractor_embedding);
        if (relevant_score <= distractor_score) {
          return RecallFixture{
              .query_text = query_text,
              .relevant_doc_text = relevant_doc_text,
              .distractor_doc_text = distractor_doc_text,
          };
        }
      }
    }
  }

  throw std::runtime_error(
      "memory vector recall quality test could not find a deterministic local-baseline miss fixture");
}

class SemanticEmbeddingTransport final : public dasall::llm::ILLMTransport {
 public:
  explicit SemanticEmbeddingTransport(RecallFixture fixture)
      : fixture_(std::move(fixture)) {}

  [[nodiscard]] dasall::llm::LLMTransportResponse send(
      const dasall::llm::LLMTransportRequest& request) override {
    ++send_calls_;

    const std::vector<float> embedding = classify_embedding(request.body);
    std::string body = "{\"data\":[{\"embedding\":[";
    for (std::size_t index = 0U; index < embedding.size(); ++index) {
      if (index != 0U) {
        body += ',';
      }
      body += std::to_string(embedding[index]);
    }
    body += "],\"index\":0}],\"model\":\"semantic-test\"}";

    return dasall::llm::LLMTransportResponse{
        .status_code = 200U,
        .body = std::move(body),
        .error_message = {},
    };
  }

  int send_calls_ = 0;

 private:
  [[nodiscard]] std::vector<float> classify_embedding(
      const std::string& payload) const {
    if (payload.find(fixture_.query_text) != std::string::npos ||
        payload.find(fixture_.relevant_doc_text) != std::string::npos) {
      return {1.0F, 0.0F, 0.0F};
    }

    if (payload.find(fixture_.distractor_doc_text) != std::string::npos) {
      return {0.0F, 1.0F, 0.0F};
    }

    return {0.0F, 0.0F, 1.0F};
  }

  RecallFixture fixture_;
};

class ScoringSqliteVssDriver final : public dasall::memory::SqliteVssVectorBackend::Driver {
 public:
  [[nodiscard]] bool reports_available() const override {
    return true;
  }

  [[nodiscard]] dasall::memory::StoreResult initialize(
      sqlite3* db,
      int embedding_dimension) override {
    if (db == nullptr || embedding_dimension <= 0) {
      return dasall::memory::StoreResult::failure(
          dasall::contracts::ResultCode::RuntimeRetryExhausted,
          "scoring driver requires sqlite handle and a positive embedding dimension");
    }

    embedding_dimension_ = embedding_dimension;
    return dasall::memory::StoreResult::success("scoring-driver-init");
  }

  [[nodiscard]] dasall::memory::StoreResult upsert(
      sqlite3* db,
      const dasall::memory::VectorDocument& document,
      const std::vector<float>& embedding) override {
    if (db == nullptr || embedding.empty()) {
      return dasall::memory::StoreResult::failure(
          dasall::contracts::ResultCode::RuntimeRetryExhausted,
          "scoring driver requires sqlite handle and a non-empty embedding");
    }

    const auto existing = std::find_if(
        entries_.begin(),
        entries_.end(),
        [&document](const auto& entry) {
          return entry.document.doc_id == document.doc_id;
        });
    if (existing != entries_.end()) {
      existing->document = document;
      existing->embedding = embedding;
      return dasall::memory::StoreResult::success(document.doc_id);
    }

    entries_.push_back(StoredEntry{.document = document, .embedding = embedding});
    return dasall::memory::StoreResult::success(document.doc_id);
  }

  [[nodiscard]] std::vector<dasall::memory::VectorHit> search(
      sqlite3* db,
      const std::vector<float>& query_embedding,
      int top_k) const override {
    if (db == nullptr || query_embedding.empty() || top_k <= 0) {
      return {};
    }

    std::vector<ScoredHit> scored_hits;
    scored_hits.reserve(entries_.size());
    for (std::size_t index = 0U; index < entries_.size(); ++index) {
      scored_hits.push_back(ScoredHit{
          .insertion_order = index,
          .score = dot_product(query_embedding, entries_[index].embedding),
          .hit = dasall::memory::VectorHit{
              .doc_id = entries_[index].document.doc_id,
              .doc_type = entries_[index].document.doc_type,
              .score = dot_product(query_embedding, entries_[index].embedding),
              .text_snippet = entries_[index].document.text,
          },
      });
    }

    std::stable_sort(scored_hits.begin(), scored_hits.end(), [](const ScoredHit& left,
                                                                const ScoredHit& right) {
      return left.score > right.score;
    });

    std::vector<dasall::memory::VectorHit> hits;
    const std::size_t result_count =
        std::min<std::size_t>(scored_hits.size(), static_cast<std::size_t>(top_k));
    hits.reserve(result_count);
    for (std::size_t index = 0U; index < result_count; ++index) {
      hits.push_back(scored_hits[index].hit);
    }
    return hits;
  }

  [[nodiscard]] int indexed_doc_count(sqlite3*) const override {
    return static_cast<int>(entries_.size());
  }

  [[nodiscard]] dasall::memory::StoreResult rebuild_index(sqlite3* db) override {
    if (db == nullptr || embedding_dimension_ <= 0) {
      return dasall::memory::StoreResult::failure(
          dasall::contracts::ResultCode::RuntimeRetryExhausted,
          "scoring driver rebuild requires initialization");
    }

    return dasall::memory::StoreResult::success("scoring-driver-rebuild");
  }

 private:
  struct StoredEntry {
    dasall::memory::VectorDocument document;
    std::vector<float> embedding;
  };

  struct ScoredHit {
    std::size_t insertion_order = 0U;
    float score = 0.0F;
    dasall::memory::VectorHit hit;
  };

  std::vector<StoredEntry> entries_;
  int embedding_dimension_ = 0;
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

[[nodiscard]] float recall_at_1(const std::vector<dasall::memory::VectorHit>& hits,
                                const std::string& expected_doc_id) {
  if (hits.empty()) {
    return 0.0F;
  }

  return hits.front().doc_id == expected_doc_id ? 1.0F : 0.0F;
}

void upsert_fixture_documents(dasall::memory::SqliteVssVectorBackend& backend,
                              const RecallFixture& fixture) {
  using dasall::tests::support::assert_true;

  const auto distractor_result = backend.upsert(dasall::memory::VectorDocument{
      .doc_id = "tea-doc",
      .doc_type = "fact",
      .text = fixture.distractor_doc_text,
      .embedding = {},
  });
  assert_true(distractor_result.ok,
              "memory vector recall quality test should upsert the distractor document");

  const auto relevant_result = backend.upsert(dasall::memory::VectorDocument{
      .doc_id = "coffee-doc",
      .doc_type = "fact",
      .text = fixture.relevant_doc_text,
      .embedding = {},
  });
  assert_true(relevant_result.ok,
              "memory vector recall quality test should upsert the relevant document");
}

void test_injected_embedding_adapter_improves_vector_recall_at_k() {
  using dasall::tests::support::assert_true;

  const auto fixture = select_baseline_miss_fixture();

  dasall::memory::VectorConfig config;
  config.enabled = true;
  config.backend_type = dasall::memory::VectorBackend::SqliteVss;

  auto local_db = open_in_memory_database();
  dasall::memory::SimpleLocalEmbeddingAdapter local_adapter;
  auto local_backend = dasall::memory::SqliteVssVectorBackend(
      config,
      local_db.get(),
      &local_adapter,
      std::make_unique<ScoringSqliteVssDriver>());
  upsert_fixture_documents(local_backend, fixture);
  const auto local_hits = local_backend.search(fixture.query_text, 1);
  const float local_recall = recall_at_1(local_hits, "coffee-doc");

  auto semantic_db = open_in_memory_database();
  auto transport = std::make_shared<SemanticEmbeddingTransport>(fixture);
  dasall::apps::runtime_support::LLMBackedEmbeddingAdapter::Options options;
  options.provider =
      dasall::apps::runtime_support::LLMBackedEmbeddingAdapter::ProviderConfig{
          .provider_id = "semantic-test",
          .model_id = "semantic-embedding",
          .base_url = "https://embedding.example/v1",
          .auth_ref = "profile://embedding.default",
          .base_url_alias = "semantic.test",
          .snapshot_version = "semantic-test@2026.06.02",
          .timeout_ms = 5000U,
      };
  options.composition_owner = "memory.vector.recall-quality";
  dasall::apps::runtime_support::LLMBackedEmbeddingAdapter semantic_adapter(
      transport,
      nullptr,
      std::move(options));
  auto semantic_backend = dasall::memory::SqliteVssVectorBackend(
      config,
      semantic_db.get(),
      &semantic_adapter,
      std::make_unique<ScoringSqliteVssDriver>());
  upsert_fixture_documents(semantic_backend, fixture);
  const auto semantic_hits = semantic_backend.search(fixture.query_text, 1);
  const float semantic_recall = recall_at_1(semantic_hits, "coffee-doc");

  assert_true(local_recall == 0.0F,
              "memory vector recall quality test should first prove the local hash baseline misses the chosen semantic fixture");
  assert_true(semantic_recall == 1.0F,
              "memory vector recall quality test should recover the relevant document after injecting the semantic embedding adapter");
  assert_true(semantic_recall > local_recall,
              "memory vector recall quality test should show recall@1 improvement once the injected embedding adapter is used");
  assert_true(transport->send_calls_ == 3,
              "memory vector recall quality test should drive the embedding provider for both upserts and the query search");
}

}  // namespace

int main() {
  try {
    test_injected_embedding_adapter_improves_vector_recall_at_k();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}