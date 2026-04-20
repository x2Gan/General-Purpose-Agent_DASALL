#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "config/MemoryConfig.h"
#include "support/TestAssertions.h"
#include "vector/IEmbeddingAdapter.h"
#include "vector/VectorMemoryIndexAdapter.h"

namespace {

class CountingEmbeddingAdapter final : public dasall::memory::IEmbeddingAdapter {
 public:
  [[nodiscard]] std::vector<float> embed(const std::string& text) const override {
    ++embed_call_count_;
    last_text_ = text;
    return {1.0F, 2.0F, 3.0F};
  }

  [[nodiscard]] int dimension() const override {
    return 3;
  }

  [[nodiscard]] int embed_call_count() const {
    return embed_call_count_;
  }

  [[nodiscard]] std::string last_text() const {
    return last_text_;
  }

 private:
  mutable int embed_call_count_ = 0;
  mutable std::string last_text_;
};

void test_unavailable_vector_memory_adapter_reports_none_backend_when_vector_is_disabled() {
  using dasall::memory::UnavailableVectorMemoryIndexAdapter;
  using dasall::memory::VectorConfig;
  using dasall::memory::VectorDocument;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  VectorConfig config;
  config.enabled = false;
  config.backend_type = dasall::memory::VectorBackend::SqliteVss;

  CountingEmbeddingAdapter embedding_adapter;
  UnavailableVectorMemoryIndexAdapter adapter(config, &embedding_adapter);

  VectorDocument document;
  document.doc_id = "fact-001";
  document.doc_type = "fact";
  document.text = "The shell command succeeded";

  const auto upsert_result = adapter.upsert(document);
  const auto search_hits = adapter.search("shell command", 5);
  const auto rebuild_result = adapter.rebuild_index();
  const auto health = adapter.health();

  assert_true(!adapter.is_available(),
              "vector adapter should report unavailable when vector config is disabled");
  assert_true(upsert_result.ok,
              "unavailable vector adapter should downgrade upsert to a no-op success result");
  assert_true(!upsert_result.result_code.has_value(),
              "unavailable vector adapter should not surface an error code for no-op upsert");
  assert_true(search_hits.empty(),
              "unavailable vector adapter should return no search hits");
  assert_true(rebuild_result.ok,
              "unavailable vector adapter should downgrade rebuild to a no-op success result");
  assert_equal("none", health.backend_type,
               "disabled vector config should collapse the health backend type to none");
  assert_true(!health.available,
              "disabled vector config should keep vector health unavailable");
  assert_equal(0, health.indexed_doc_count,
               "unavailable vector adapter should not index documents");
  assert_equal(0, embedding_adapter.embed_call_count(),
               "unavailable vector adapter should not invoke embedding generation on no-op operations");
}

void test_unavailable_vector_memory_adapter_preserves_requested_backend_for_availability_gate() {
  using dasall::memory::UnavailableVectorMemoryIndexAdapter;
  using dasall::memory::VectorConfig;
  using dasall::memory::VectorDocument;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  VectorConfig config;
  config.enabled = true;
  config.backend_type = dasall::memory::VectorBackend::SqliteVss;
  config.search_top_k = 7;

  CountingEmbeddingAdapter embedding_adapter;
  UnavailableVectorMemoryIndexAdapter adapter(config, &embedding_adapter);

  VectorDocument document;
  document.doc_id = "summary-001";
  document.doc_type = "summary";
  document.text = "Summaries stay searchable when vector backend appears later";

  const auto upsert_result = adapter.upsert(document);
  const auto search_hits = adapter.search("summaries searchable", config.search_top_k);
  const auto health = adapter.health();

  assert_true(!adapter.is_available(),
              "unavailable baseline should remain unavailable even when vector is enabled but no backend is wired");
  assert_true(upsert_result.ok,
              "unavailable baseline should preserve a no-op success result when callers still invoke upsert");
  assert_true(search_hits.empty(),
              "unavailable baseline should preserve an empty result set for search");
  assert_equal("sqlite-vss", health.backend_type,
               "enabled vector config should preserve the requested backend type in health output");
  assert_equal(0, embedding_adapter.embed_call_count(),
               "availability gating should prevent embedding calls while the vector backend is unavailable");
}

}  // namespace

int main() {
  try {
    test_unavailable_vector_memory_adapter_reports_none_backend_when_vector_is_disabled();
    test_unavailable_vector_memory_adapter_preserves_requested_backend_for_availability_gate();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}