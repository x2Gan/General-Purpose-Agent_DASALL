#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "FakeMemoryStore.h"
#include "conflict/MemoryConflictResolver.h"
#include "support/TestAssertions.h"
#include "vector/VectorMemoryIndexAdapter.h"
#include "working/IWorkingMemoryBoard.h"
#include "writeback/WritebackCoordinator.h"

namespace {

class DerivedFailureStore final : public dasall::memory::IMemoryStore {
 public:
  [[nodiscard]] std::optional<dasall::contracts::ResultCode> open(
      const dasall::memory::MemoryConfig& config) override {
    return delegate_.open(config);
  }

  void close() noexcept override {
    delegate_.close();
  }

  [[nodiscard]] std::unique_ptr<dasall::memory::IStoreTransaction> begin_immediate()
      override {
    return delegate_.begin_immediate();
  }

  [[nodiscard]] dasall::memory::SessionLoadBundle load_session_bundle(
      const dasall::memory::SessionLoadRequest& request) const override {
    return delegate_.load_session_bundle(request);
  }

  [[nodiscard]] dasall::memory::StoreResult create_session(
      const dasall::contracts::Session& session) override {
    return delegate_.create_session(session);
  }

  [[nodiscard]] dasall::memory::StoreResult append_turn(
      const dasall::contracts::Turn& turn) override {
    return delegate_.append_turn(turn);
  }

  [[nodiscard]] dasall::memory::StoreResult update_session_active(
      const std::string& session_id,
      std::int64_t last_active_at) override {
    return delegate_.update_session_active(session_id, last_active_at);
  }

  [[nodiscard]] dasall::memory::StoreResult upsert_summary(
      const dasall::contracts::SummaryMemory& summary) override {
    return delegate_.upsert_summary(summary);
  }

  [[nodiscard]] std::optional<dasall::contracts::SummaryMemory> load_latest_summary(
      const std::string& session_id) const override {
    return delegate_.load_latest_summary(session_id);
  }

  [[nodiscard]] dasall::memory::FactQueryResult query_facts(
      const dasall::memory::FactQuery& query) const override {
    (void)query;
    return {};
  }

  [[nodiscard]] dasall::memory::StoreResult insert_fact(
      const dasall::contracts::MemoryFact& fact) override {
    return dasall::memory::StoreResult::failure(
        dasall::contracts::ResultCode::ValidationFieldMissing,
        std::string{"fact insert failed: "} + fact.fact_id.value_or("fact"));
  }

  [[nodiscard]] dasall::memory::StoreResult supersede_fact(
      const std::string& old_fact_id,
      const std::string& new_fact_id) override {
    return delegate_.supersede_fact(old_fact_id, new_fact_id);
  }

  [[nodiscard]] dasall::memory::ExperienceQueryResult query_experiences(
      const dasall::memory::ExperienceQuery& query) const override {
    return delegate_.query_experiences(query);
  }

  [[nodiscard]] dasall::memory::StoreResult insert_experience(
      const dasall::contracts::ExperienceMemory& experience) override {
    return dasall::memory::StoreResult::failure(
        dasall::contracts::ResultCode::ValidationFieldMissing,
        std::string{"experience insert failed: "} +
            experience.experience_id.value_or("experience"));
  }

  [[nodiscard]] std::int64_t count_turns(const std::string& session_id) const override {
    return delegate_.count_turns(session_id);
  }

  [[nodiscard]] dasall::memory::StoreResult quarantine_record(
      const std::string& object_type,
      const std::string& object_id,
      const std::string& reason) override {
    return delegate_.quarantine_record(object_type, object_id, reason);
  }

 private:
  dasall::tests::mocks::FakeMemoryStore delegate_;
};

class FailingVectorMemoryIndexAdapter final : public dasall::memory::VectorMemoryIndexAdapter {
 public:
  [[nodiscard]] bool is_available() const override {
    return true;
  }

  [[nodiscard]] dasall::memory::StoreResult upsert(
      const dasall::memory::VectorDocument& doc) override {
    upserted_doc_ids.push_back(doc.doc_id);
    return dasall::memory::StoreResult::failure(
        dasall::contracts::ResultCode::ValidationFieldMissing,
        std::string{"vector failure"});
  }

  [[nodiscard]] std::vector<dasall::memory::VectorHit> search(
      const std::string& query_text,
      int top_k) const override {
    (void)query_text;
    (void)top_k;
    return {};
  }

  [[nodiscard]] dasall::memory::VectorIndexHealth health() const override {
    return dasall::memory::VectorIndexHealth{
        .available = true,
        .indexed_doc_count = 0,
        .last_rebuild_at = 0,
        .backend_type = "sqlite-vss",
    };
  }

  [[nodiscard]] dasall::memory::StoreResult rebuild_index() override {
    return dasall::memory::StoreResult::success("rebuild");
  }

  std::vector<std::string> upserted_doc_ids;
};

[[nodiscard]] bool contains_warning(const std::vector<std::string>& warnings,
                                    const std::string& expected) {
  return std::find(warnings.begin(), warnings.end(), expected) != warnings.end();
}

void test_writeback_coordinator_marks_partial_without_rolling_back_core_transaction() {
  using dasall::tests::support::assert_true;

  DerivedFailureStore store;
  auto working_board = dasall::memory::create_working_memory_board();
  FailingVectorMemoryIndexAdapter vector_index;
  auto conflict_resolver =
      std::make_unique<dasall::memory::MemoryConflictResolver>(store);
  dasall::memory::WritebackCoordinator coordinator(
      store, store, store, store, store,
      std::move(conflict_resolver), *working_board, &vector_index);

  dasall::memory::MemoryWritebackRequest request;
  request.session_id = "session-021-partial";
  request.turn.turn_id = "turn-021-partial";
  request.turn.session_id = request.session_id;
  request.turn.user_input = "记录一条可能冲突的事实";
  request.turn.agent_response = "核心事务先落地，再尝试派生写入。";
  request.turn.created_at = 2000;
    dasall::memory::FactCandidate fact_candidate;
    fact_candidate.fact.fact_text = "network mode enabled";
    fact_candidate.fact.source_turn_ids =
      std::vector<std::string>{"turn-021-partial"};
    fact_candidate.fact.confidence_score = 90;
    fact_candidate.fact.created_at = 2000;
    fact_candidate.fact.fact_type = "status";
    fact_candidate.extraction_source = "turn";
    request.fact_candidates.push_back(std::move(fact_candidate));

    dasall::memory::ExperienceCandidate experience_candidate;
    experience_candidate.experience.lesson_summary =
      "先写核心事务，再处理派生写入";
    experience_candidate.experience.trigger_condition =
      "fact write partially fails";
    experience_candidate.experience.recommended_action =
      "report partial warning";
    experience_candidate.experience.created_at = 2000;
    experience_candidate.extraction_source = "reflection";
    request.experience_candidates.push_back(std::move(experience_candidate));

  const auto result = coordinator.persist(request);

  assert_true(!result.result_code.has_value(),
              "writeback coordinator should keep the overall writeback successful when only derived writes fail");
  assert_true(result.persisted_turn_id == std::optional<std::string>{"turn-021-partial"},
              "writeback coordinator should still persist the core turn on the partial path");
  assert_true(result.partial,
              "writeback coordinator should mark the result as partial when fact or experience persistence fails");
  assert_true(result.fact_ids.empty() && result.experience_ids.empty(),
              "writeback coordinator should not report derived ids when the derived writes fail");
  assert_true(contains_warning(result.warnings, "partial_writeback_warning") &&
                  contains_warning(result.warnings, "fact_write_failed") &&
                  contains_warning(result.warnings, "experience_write_failed"),
              "writeback coordinator should surface partial-write warnings for derived write failures");
  assert_true(contains_warning(result.warnings, "vector_sidecar_failed"),
              "writeback coordinator should report vector sidecar failures without failing the writeback");
  assert_true(store.count_turns("session-021-partial") == 1,
              "writeback coordinator should keep the core transaction committed on the partial path");
  assert_true(working_board->get_slot("session-021-partial", "latest_turn_id").has_value(),
              "writeback coordinator should still update the working board after a partial writeback");
  assert_true(!vector_index.upserted_doc_ids.empty() &&
                  vector_index.upserted_doc_ids.front() == "turn-021-partial",
              "writeback coordinator should still attempt the vector sidecar after the core transaction succeeds");
}

}  // namespace

int main() {
  try {
    test_writeback_coordinator_marks_partial_without_rolling_back_core_transaction();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}