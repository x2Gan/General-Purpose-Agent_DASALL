#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef DASALL_SQL_MEMORY_DIR
#error DASALL_SQL_MEMORY_DIR must be defined for sqlite writeback coverage
#endif

#include "conflict/MemoryConflictResolver.h"
#include "store/sqlite/SqliteMemoryStore.h"
#include "support/TestAssertions.h"
#include "vector/VectorMemoryIndexAdapter.h"
#include "working/IWorkingMemoryBoard.h"
#include "writeback/WritebackCoordinator.h"

namespace {

class SpyVectorMemoryIndexAdapter final : public dasall::memory::VectorMemoryIndexAdapter {
 public:
  bool available = true;
  bool fail_upsert = false;
  std::vector<dasall::memory::VectorDocument> upserted_documents;

  [[nodiscard]] bool is_available() const override {
    return available;
  }

  [[nodiscard]] dasall::memory::StoreResult upsert(
      const dasall::memory::VectorDocument& doc) override {
    upserted_documents.push_back(doc);
    if (fail_upsert) {
      return dasall::memory::StoreResult::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          std::string{"vector upsert failed"});
    }
    return dasall::memory::StoreResult::success(doc.doc_id);
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
        .available = available,
        .indexed_doc_count = static_cast<int>(upserted_documents.size()),
        .last_rebuild_at = 0,
        .backend_type = "spy",
    };
  }

  [[nodiscard]] dasall::memory::StoreResult rebuild_index() override {
    return dasall::memory::StoreResult::success("rebuild");
  }
};

class CommitFailingStore final : public dasall::memory::IMemoryStore {
 public:
  class Transaction final : public dasall::memory::IStoreTransaction {
   public:
    explicit Transaction(CommitFailingStore& owner) : owner_(owner) {}

    ~Transaction() override {
      if (active_) {
        rollback();
      }
    }

    [[nodiscard]] std::optional<dasall::contracts::ResultCode> commit() override {
      owner_.commit_attempts_ += 1;
      return dasall::contracts::ResultCode::RuntimeRetryExhausted;
    }

    void rollback() noexcept override {
      if (!active_) {
        return;
      }

      active_ = false;
      owner_.pending_session_.reset();
      owner_.pending_turns_.clear();
      owner_.pending_summary_.reset();
      owner_.transaction_active_ = false;
    }

   private:
    CommitFailingStore& owner_;
    bool active_ = true;
  };

  [[nodiscard]] std::optional<dasall::contracts::ResultCode> open(
      const dasall::memory::MemoryConfig& config) override {
    (void)config;
    return std::nullopt;
  }

  void close() noexcept override {
    transaction_active_ = false;
  }

  [[nodiscard]] std::unique_ptr<dasall::memory::IStoreTransaction> begin_immediate()
      override {
    transaction_active_ = true;
    pending_session_.reset();
    pending_turns_.clear();
    pending_summary_.reset();
    return std::make_unique<Transaction>(*this);
  }

  [[nodiscard]] dasall::memory::SessionLoadBundle load_session_bundle(
      const dasall::memory::SessionLoadRequest& request) const override {
    dasall::memory::SessionLoadBundle bundle;
    if (committed_session_.has_value() &&
        committed_session_->session_id == std::optional<std::string>{request.session_id}) {
      bundle.session = *committed_session_;
      bundle.recent_turns = committed_turns_;
      bundle.total_turn_count = static_cast<int>(committed_turns_.size());
    }
    return bundle;
  }

  [[nodiscard]] dasall::memory::StoreResult create_session(
      const dasall::contracts::Session& session) override {
    pending_session_ = session;
    return dasall::memory::StoreResult::success(session.session_id);
  }

  [[nodiscard]] dasall::memory::StoreResult append_turn(
      const dasall::contracts::Turn& turn) override {
    pending_turns_.push_back(turn);
    return dasall::memory::StoreResult::success(turn.turn_id);
  }

  [[nodiscard]] dasall::memory::StoreResult update_session_active(
      const std::string& session_id,
      std::int64_t last_active_at) override {
    (void)session_id;
    if (pending_session_.has_value()) {
      pending_session_->last_active_at = last_active_at;
    }
    return dasall::memory::StoreResult::success(session_id);
  }

  [[nodiscard]] dasall::memory::StoreResult upsert_summary(
      const dasall::contracts::SummaryMemory& summary) override {
    pending_summary_ = summary;
    return dasall::memory::StoreResult::success(summary.summary_id);
  }

  [[nodiscard]] std::optional<dasall::contracts::SummaryMemory> load_latest_summary(
      const std::string& session_id) const override {
    if (committed_summary_.has_value() &&
        committed_summary_->session_id == std::optional<std::string>{session_id}) {
      return committed_summary_;
    }
    return std::nullopt;
  }

  [[nodiscard]] dasall::memory::FactQueryResult query_facts(
      const dasall::memory::FactQuery& query) const override {
    (void)query;
    return {};
  }

  [[nodiscard]] dasall::memory::StoreResult insert_fact(
      const dasall::contracts::MemoryFact& fact) override {
    return dasall::memory::StoreResult::success(fact.fact_id);
  }

  [[nodiscard]] dasall::memory::StoreResult supersede_fact(
      const std::string& old_fact_id,
      const std::string& new_fact_id) override {
    return dasall::memory::StoreResult::success(old_fact_id + ":" + new_fact_id);
  }

  [[nodiscard]] dasall::memory::ExperienceQueryResult query_experiences(
      const dasall::memory::ExperienceQuery& query) const override {
    (void)query;
    return {};
  }

  [[nodiscard]] dasall::memory::StoreResult insert_experience(
      const dasall::contracts::ExperienceMemory& experience) override {
    return dasall::memory::StoreResult::success(experience.experience_id);
  }

  [[nodiscard]] std::int64_t count_turns(const std::string& session_id) const override {
    return (committed_session_.has_value() &&
            committed_session_->session_id == std::optional<std::string>{session_id})
               ? static_cast<std::int64_t>(committed_turns_.size())
               : 0;
  }

  [[nodiscard]] dasall::memory::StoreResult quarantine_record(
      const std::string& object_type,
      const std::string& object_id,
      const std::string& reason) override {
    return dasall::memory::StoreResult::success(object_type + ":" + object_id + ":" + reason);
  }

  [[nodiscard]] int commit_attempts_for_test() const {
    return commit_attempts_;
  }

 private:
  bool transaction_active_ = false;
  int commit_attempts_ = 0;
  std::optional<dasall::contracts::Session> committed_session_;
  std::vector<dasall::contracts::Turn> committed_turns_;
  std::optional<dasall::contracts::SummaryMemory> committed_summary_;
  std::optional<dasall::contracts::Session> pending_session_;
  std::vector<dasall::contracts::Turn> pending_turns_;
  std::optional<dasall::contracts::SummaryMemory> pending_summary_;
};

std::filesystem::path make_temp_database_path() {
  const auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
  return std::filesystem::temp_directory_path() /
         ("dasall-writeback-core-" + std::to_string(timestamp) + ".db");
}

[[nodiscard]] dasall::memory::MemoryWritebackRequest make_request(
    const std::string& session_id,
    const std::string& turn_id,
    const std::string& user_input,
    const std::string& agent_response) {
  dasall::memory::MemoryWritebackRequest request;
  request.session_id = session_id;
  request.turn.turn_id = turn_id;
  request.turn.session_id = session_id;
  request.turn.user_input = user_input;
  request.turn.agent_response = agent_response;
  request.turn.created_at = 2000;
    request.summary_candidate = dasall::contracts::SummaryMemory{};
    request.summary_candidate->summary_text = "核心事务摘要";
    request.summary_candidate->decisions_made =
      std::vector<std::string>{"记录核心事务完成"};
    request.summary_candidate->confirmed_facts =
      std::vector<std::string>{"writeback core is active"};
    request.summary_candidate->tool_outcomes =
      std::vector<std::string>{"tool_calls: none"};
  request.side_effect_report_ref = "side-effect-021";
  return request;
}

void test_writeback_coordinator_persists_core_transaction_and_updates_working_board() {
  using dasall::tests::support::assert_true;

  dasall::memory::MemoryConfig config;
  config.storage.backend = "sqlite";
  config.storage.db_path = make_temp_database_path().string();
  config.storage.migrations_dir = DASALL_SQL_MEMORY_DIR;

  auto store = dasall::memory::store::sqlite::create_sqlite_memory_store();
  const auto open_result = store->open(config);
  if (open_result.has_value()) {
    throw std::runtime_error("failed to open sqlite store for writeback core test");
  }

  auto working_board = dasall::memory::create_working_memory_board();
  SpyVectorMemoryIndexAdapter vector_index;
  auto conflict_resolver =
      std::make_unique<dasall::memory::MemoryConflictResolver>(*store);
  dasall::memory::WritebackCoordinator coordinator(
      *store, std::move(conflict_resolver), *working_board, &vector_index);

  const auto request = make_request("session-021-core", "turn-021-core",
                                    "记录最近一次执行结果", "已写入核心事务");
  const auto result = coordinator.persist(request);

  assert_true(!result.result_code.has_value(),
              "writeback coordinator should succeed on the sqlite core path");
  assert_true(result.persisted_turn_id == std::optional<std::string>{"turn-021-core"},
              "writeback coordinator should return the persisted turn id");
  assert_true(result.summary_id.has_value(),
              "writeback coordinator should synthesize and persist a summary id");

  const auto bundle = store->load_session_bundle(
      dasall::memory::SessionLoadRequest{.session_id = "session-021-core",
                                         .recent_turn_limit = 4});
  assert_true(bundle.session.session_id == std::optional<std::string>{"session-021-core"},
              "writeback coordinator should create the session on first writeback");
  assert_true(bundle.total_turn_count == 1 && bundle.recent_turns.size() == 1U,
              "writeback coordinator should persist the turn inside the core transaction");

  const auto summary = store->load_latest_summary("session-021-core");
  assert_true(summary.has_value() && summary->summary_id == result.summary_id,
              "writeback coordinator should persist the normalized summary inside the core transaction");

  const auto latest_turn_slot =
      working_board->get_slot("session-021-core", "latest_turn_id");
  const auto latest_summary_slot =
      working_board->get_slot("session-021-core", "latest_summary_id");
  assert_true(latest_turn_slot.has_value() && latest_turn_slot->value == "turn-021-core",
              "writeback coordinator should update latest_turn_id as the working-board owner");
  assert_true(latest_summary_slot.has_value() &&
                  latest_summary_slot->value == *result.summary_id,
              "writeback coordinator should update latest_summary_id as the working-board owner");
  assert_true(!vector_index.upserted_documents.empty() &&
                  vector_index.upserted_documents.front().doc_id == "turn-021-core",
              "writeback coordinator should upsert the persisted turn into the vector sidecar when available");

  store->close();
}

void test_writeback_coordinator_rolls_back_when_commit_fails() {
  using dasall::tests::support::assert_true;

  CommitFailingStore store;
  auto working_board = dasall::memory::create_working_memory_board();
  auto conflict_resolver =
      std::make_unique<dasall::memory::MemoryConflictResolver>(store);
  dasall::memory::WritebackCoordinator coordinator(
      store, std::move(conflict_resolver), *working_board);

  const auto request = make_request("session-021-rollback", "turn-021-rollback",
                                    "验证 commit 失败回滚", "核心事务必须失败回滚");
  const auto result = coordinator.persist(request);

  assert_true(result.retryable_storage_failure,
              "writeback coordinator should mark commit failures as retryable storage failures");
  assert_true(result.degraded,
              "writeback coordinator should surface degraded execution when the core transaction fails");
  assert_true(!result.persisted_turn_id.has_value() && !result.summary_id.has_value(),
              "writeback coordinator should not expose persisted ids after a failed commit");
  assert_true(store.commit_attempts_for_test() == 3,
              "writeback coordinator should exhaust the bounded local retry budget on commit failure");
  assert_true(store.count_turns("session-021-rollback") == 0,
              "writeback coordinator should leave no committed turns after a failed commit");
  assert_true(!store.load_latest_summary("session-021-rollback").has_value(),
              "writeback coordinator should leave no committed summary after a failed commit");
  assert_true(!working_board->get_slot("session-021-rollback", "latest_turn_id").has_value(),
              "writeback coordinator should not mutate the working board when the core transaction fails");
}

}  // namespace

int main() {
  try {
    test_writeback_coordinator_persists_core_transaction_and_updates_working_board();
    test_writeback_coordinator_rolls_back_when_commit_fails();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}