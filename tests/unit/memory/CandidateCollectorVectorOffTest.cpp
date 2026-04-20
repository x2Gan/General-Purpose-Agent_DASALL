#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "FakeMemoryStore.h"
#include "config/MemoryConfig.h"
#include "context/CandidateCollector.h"
#include "support/TestAssertions.h"
#include "vector/VectorMemoryIndexAdapter.h"
#include "working/IWorkingMemoryBoard.h"

namespace {

class TrackingVectorMemoryIndexAdapter final : public dasall::memory::VectorMemoryIndexAdapter {
 public:
  [[nodiscard]] bool is_available() const override {
    return true;
  }

  [[nodiscard]] dasall::memory::StoreResult upsert(
      const dasall::memory::VectorDocument& doc) override {
    (void)doc;
    return dasall::memory::StoreResult::success();
  }

  [[nodiscard]] std::vector<dasall::memory::VectorHit> search(
      const std::string& query_text,
      int top_k) const override {
    ++search_call_count_;
    last_query_text_ = query_text;
    last_top_k_ = top_k;
    return {{
        .doc_id = "ignored",
        .doc_type = "fact",
        .score = 1.0F,
        .text_snippet = "should never be returned when vector is disabled",
    }};
  }

  [[nodiscard]] dasall::memory::VectorIndexHealth health() const override {
    return dasall::memory::VectorIndexHealth{
        .available = true,
        .indexed_doc_count = 1,
        .last_rebuild_at = 0,
        .backend_type = "sqlite-vss",
    };
  }

  [[nodiscard]] dasall::memory::StoreResult rebuild_index() override {
    return dasall::memory::StoreResult::success();
  }

  [[nodiscard]] int search_call_count() const {
    return search_call_count_;
  }

  [[nodiscard]] std::string last_query_text() const {
    return last_query_text_;
  }

  [[nodiscard]] int last_top_k() const {
    return last_top_k_;
  }

 private:
  mutable int search_call_count_ = 0;
  mutable int last_top_k_ = 0;
  mutable std::string last_query_text_;
};

void test_candidate_collector_skips_vector_search_when_vector_is_disabled() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  dasall::tests::mocks::FakeMemoryStore store;
  dasall::contracts::Session session;
  session.session_id = "session-016-vector-off";
  session.turn_ids = std::vector<std::string>{};
  session.created_at = 1000;
  if (!store.create_session(session).ok) {
    throw std::runtime_error("failed to seed vector-off session");
  }

  auto board = dasall::memory::create_working_memory_board();
  board->set_slot("session-016-vector-off", dasall::memory::WorkingMemorySlot{
                                                .key = "active_goal",
                                                .value = "验证 vector disabled gate",
                                                .updated_at = 1001,
                                                .ttl_ms = 0,
                                                .source = "agent",
                                            });

  dasall::memory::MemoryConfig config;
  config.context.recent_turn_limit = 2;
  config.vector.enabled = false;
  config.vector.search_top_k = 9;

  TrackingVectorMemoryIndexAdapter vector_index;
  dasall::memory::CandidateCollector collector(
      *board, store, store, store, store, config, &vector_index);

  const auto set = collector.collect(dasall::memory::CandidateCollectRequest{
      .session_id = "session-016-vector-off",
      .stage = "plan",
      .goal_summary = "验证 vector disabled gate",
      .external_evidence = std::vector<std::string>{"local-evidence"},
  });

  assert_true(set.vector_hits.empty(),
              "candidate collector should return no vector hits when vector is disabled in config");
  assert_equal(0, vector_index.search_call_count(),
               "candidate collector should not call the vector backend when vector is disabled");
  assert_true(set.warnings.empty(),
              "candidate collector should not emit vector warnings when vector is intentionally disabled");
  assert_equal(1, static_cast<int>(set.external_evidence.size()),
               "candidate collector should still preserve external evidence when vector is disabled");
  assert_true(set.estimated_total_tokens > 0,
              "candidate collector should still estimate tokens from the remaining candidate sources");
}

}  // namespace

int main() {
  try {
    test_candidate_collector_skips_vector_search_when_vector_is_disabled();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}