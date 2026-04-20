#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "FakeMemoryStore.h"
#include "config/MemoryConfig.h"
#include "context/CandidateCollector.h"
#include "support/TestAssertions.h"
#include "vector/UnavailableVectorMemoryIndexAdapter.h"
#include "vector/VectorMemoryIndexAdapter.h"
#include "working/IWorkingMemoryBoard.h"

namespace {

class SpyVectorMemoryIndexAdapter final : public dasall::memory::VectorMemoryIndexAdapter {
 public:
  explicit SpyVectorMemoryIndexAdapter(std::vector<dasall::memory::VectorHit> hits)
      : hits_(std::move(hits)) {}

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
    return hits_;
  }

  [[nodiscard]] dasall::memory::VectorIndexHealth health() const override {
    return dasall::memory::VectorIndexHealth{
        .available = true,
        .indexed_doc_count = static_cast<int>(hits_.size()),
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
  std::vector<dasall::memory::VectorHit> hits_;
  mutable int search_call_count_ = 0;
  mutable int last_top_k_ = 0;
  mutable std::string last_query_text_;
};

class DelegatingMemoryStore final : public dasall::memory::IMemoryStore {
 public:
  explicit DelegatingMemoryStore(dasall::memory::IMemoryStore& delegate)
      : delegate_(delegate) {}

  bool fail_experience_query = false;

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
    return delegate_.query_facts(query);
  }

  [[nodiscard]] dasall::memory::StoreResult insert_fact(
      const dasall::contracts::MemoryFact& fact) override {
    return delegate_.insert_fact(fact);
  }

  [[nodiscard]] dasall::memory::StoreResult supersede_fact(
      const std::string& old_fact_id,
      const std::string& new_fact_id) override {
    return delegate_.supersede_fact(old_fact_id, new_fact_id);
  }

  [[nodiscard]] dasall::memory::ExperienceQueryResult query_experiences(
      const dasall::memory::ExperienceQuery& query) const override {
    if (fail_experience_query) {
      throw std::runtime_error("experience query failure");
    }

    return delegate_.query_experiences(query);
  }

  [[nodiscard]] dasall::memory::StoreResult insert_experience(
      const dasall::contracts::ExperienceMemory& experience) override {
    return delegate_.insert_experience(experience);
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
  dasall::memory::IMemoryStore& delegate_;
};

bool contains_warning(const std::vector<std::string>& warnings,
                      const std::string& warning) {
  return std::find(warnings.begin(), warnings.end(), warning) != warnings.end();
}

dasall::memory::MemoryConfig make_memory_config() {
  dasall::memory::MemoryConfig config;
  config.context.recent_turn_limit = 1;
  config.context.fact_confidence_floor = 80;
  config.vector.enabled = true;
  config.vector.search_top_k = 3;
  return config;
}

void seed_memory_store(dasall::tests::mocks::FakeMemoryStore& store) {
  dasall::contracts::Session session;
  session.session_id = "session-016";
  session.turn_ids = std::vector<std::string>{};
  session.user_id = "user-016";
  session.created_at = 1000;
  session.last_active_at = 1020;
  session.metadata_digest = "context-pipeline";

  if (!store.create_session(session).ok) {
    throw std::runtime_error("failed to seed candidate collector session");
  }

  dasall::contracts::Turn first_turn;
  first_turn.turn_id = "turn-016-001";
  first_turn.session_id = "session-016";
  first_turn.user_input = "先对齐 context 组装链";
  first_turn.agent_response = "先补 candidate collector";
  first_turn.created_at = 1010;
  first_turn.tags = std::vector<std::string>{"plan"};
  if (!store.append_turn(first_turn).ok) {
    throw std::runtime_error("failed to seed first candidate collector turn");
  }

  dasall::contracts::Turn second_turn;
  second_turn.turn_id = "turn-016-002";
  second_turn.session_id = "session-016";
  second_turn.user_input = "再补 budget allocator";
  second_turn.agent_response = "收口预算分配";
  second_turn.created_at = 1020;
  second_turn.tags = std::vector<std::string>{"plan", "latest"};
  if (!store.append_turn(second_turn).ok) {
    throw std::runtime_error("failed to seed second candidate collector turn");
  }

  dasall::contracts::SummaryMemory summary;
  summary.summary_id = "summary-016-001";
  summary.session_id = "session-016";
  summary.summary_text = "Context 组装链已完成 store 与 working memory 基线。";
  summary.source_turn_ids = std::vector<std::string>{"turn-016-001", "turn-016-002"};
  summary.decisions_made = std::vector<std::string>{"按原子任务串行推进"};
  summary.confirmed_facts = std::vector<std::string>{"sqlite store ready"};
  summary.tool_outcomes = std::vector<std::string>{"tests green"};
  summary.created_at = 1030;
  if (!store.upsert_summary(summary).ok) {
    throw std::runtime_error("failed to seed candidate collector summary");
  }

  dasall::contracts::MemoryFact strong_fact;
  strong_fact.fact_id = "fact-016-001";
  strong_fact.session_id = "session-016";
  strong_fact.fact_text = "用户要求每个原子任务完成后立即提交并推送。";
  strong_fact.source_turn_ids = std::vector<std::string>{"turn-016-001"};
  strong_fact.confidence_score = 95;
  strong_fact.created_at = 1040;
  strong_fact.fact_type = "constraint";
  strong_fact.tags = std::vector<std::string>{"high-confidence"};
  if (!store.insert_fact(strong_fact).ok) {
    throw std::runtime_error("failed to seed strong candidate collector fact");
  }

  dasall::contracts::MemoryFact weak_fact;
  weak_fact.fact_id = "fact-016-002";
  weak_fact.session_id = "session-016";
  weak_fact.fact_text = "低置信度事实不应进入候选集。";
  weak_fact.source_turn_ids = std::vector<std::string>{"turn-016-001"};
  weak_fact.confidence_score = 40;
  weak_fact.created_at = 1041;
  weak_fact.fact_type = "note";
  if (!store.insert_fact(weak_fact).ok) {
    throw std::runtime_error("failed to seed weak candidate collector fact");
  }

  dasall::contracts::ExperienceMemory stage_match_experience;
  stage_match_experience.experience_id = "exp-016-001";
  stage_match_experience.session_id = "session-016";
  stage_match_experience.lesson_summary = "先做代码考古再落盘。";
  stage_match_experience.trigger_condition = "stage=plan";
  stage_match_experience.recommended_action = "read boundary files first";
  stage_match_experience.created_at = 1050;
  stage_match_experience.applicable_domains = std::vector<std::string>{"memory"};
  stage_match_experience.tags = std::vector<std::string>{"stage:plan"};
  if (!store.insert_experience(stage_match_experience).ok) {
    throw std::runtime_error("failed to seed matching candidate collector experience");
  }

  dasall::contracts::ExperienceMemory stage_miss_experience;
  stage_miss_experience.experience_id = "exp-016-002";
  stage_miss_experience.session_id = "session-016";
  stage_miss_experience.lesson_summary = "执行态经验不应出现在 planning 阶段。";
  stage_miss_experience.trigger_condition = "stage=execute";
  stage_miss_experience.recommended_action = "skip for planning";
  stage_miss_experience.created_at = 1051;
  stage_miss_experience.tags = std::vector<std::string>{"stage:execute"};
  if (!store.insert_experience(stage_miss_experience).ok) {
    throw std::runtime_error("failed to seed non-matching candidate collector experience");
  }
}

void test_candidate_collector_collects_multi_source_candidates_and_estimates_tokens() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  dasall::tests::mocks::FakeMemoryStore store;
  seed_memory_store(store);

  auto board = dasall::memory::create_working_memory_board();
  dasall::memory::WorkingMemorySnapshot snapshot;
  snapshot.session_id = "session-016";
  snapshot.slots.push_back(dasall::memory::WorkingMemorySlot{
      .key = "active_goal",
      .value = "完成 Context 组装链",
      .updated_at = 2000,
      .ttl_ms = 0,
      .source = "agent",
  });
  snapshot.open_questions.push_back("CandidateCollector 是否按阶段过滤经验?");
  snapshot.ephemeral_facts.push_back("当前只允许做 016 这一轮提交。");
  board->restore_snapshot(snapshot);

  SpyVectorMemoryIndexAdapter vector_index({dasall::memory::VectorHit{
      .doc_id = "summary-016-001",
      .doc_type = "summary",
      .score = 0.92F,
      .text_snippet = "Context 组装链已完成 store 与 working memory 基线。",
  }});

  const auto config = make_memory_config();
  dasall::memory::CandidateCollector collector(*board, store, config, &vector_index);

  const auto set = collector.collect(dasall::memory::CandidateCollectRequest{
      .session_id = "session-016",
      .stage = "plan",
      .goal_summary = "完成 Context 组装链",
      .token_budget_hint = 2048,
      .latency_budget_ms = 50,
      .external_evidence = std::vector<std::string>{"docs-evidence-016"},
  });

  assert_equal("session-016", set.working_snapshot.session_id,
               "candidate collector should preserve the working-memory session id");
  assert_equal(1, static_cast<int>(set.working_snapshot.slots.size()),
               "candidate collector should pull one working-memory slot from the board");
  assert_equal(1, static_cast<int>(set.working_snapshot.open_questions.size()),
               "candidate collector should preserve working-memory open questions");
  assert_equal(1, static_cast<int>(set.working_snapshot.ephemeral_facts.size()),
               "candidate collector should preserve working-memory ephemeral facts");

  assert_equal(2, set.session_bundle.total_turn_count,
               "candidate collector should surface the full turn count from the store");
  assert_equal(1, static_cast<int>(set.session_bundle.recent_turns.size()),
               "candidate collector should cap recent turns using context.recent_turn_limit");
  assert_true(set.session_bundle.recent_turns.front().turn_id ==
                  std::optional<std::string>{"turn-016-002"},
              "candidate collector should return the newest turn first in the recent window");

  assert_true(set.latest_summary.has_value(),
              "candidate collector should load the latest summary when one exists");
  assert_true(set.latest_summary->summary_id ==
                  std::optional<std::string>{"summary-016-001"},
              "candidate collector should return the persisted latest summary");

  assert_equal(1, static_cast<int>(set.relevant_facts.size()),
               "candidate collector should filter facts using the configured confidence floor");
  assert_true(set.relevant_facts.front().fact_id ==
                  std::optional<std::string>{"fact-016-001"},
              "candidate collector should keep the surviving high-confidence fact");

  assert_equal(1, static_cast<int>(set.relevant_experiences.size()),
               "candidate collector should filter experiences by the requested stage");
  assert_true(set.relevant_experiences.front().experience_id ==
                  std::optional<std::string>{"exp-016-001"},
              "candidate collector should keep the stage-matching experience");

  assert_equal(1, static_cast<int>(set.external_evidence.size()),
               "candidate collector should preserve external evidence slices");
  assert_equal(1, static_cast<int>(set.vector_hits.size()),
               "candidate collector should include vector hits when the adapter is available");
  assert_equal(1, vector_index.search_call_count(),
               "candidate collector should issue one vector search for the goal summary");
  assert_equal("完成 Context 组装链", vector_index.last_query_text(),
               "candidate collector should use goal_summary as the vector search query");
  assert_equal(3, vector_index.last_top_k(),
               "candidate collector should honor vector.search_top_k when searching");

  assert_true(set.warnings.empty(),
              "candidate collector should not emit warnings on the happy path");
  assert_true(set.estimated_total_tokens > 0,
              "candidate collector should compute a positive token estimate for non-empty candidates");
}

void test_candidate_collector_degrades_when_experience_query_fails_and_vector_is_unavailable() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  dasall::tests::mocks::FakeMemoryStore base_store;
  seed_memory_store(base_store);
  DelegatingMemoryStore store(base_store);
  store.fail_experience_query = true;

  auto board = dasall::memory::create_working_memory_board();
  board->set_slot("session-016", dasall::memory::WorkingMemorySlot{
                                   .key = "active_goal",
                                   .value = "完成 Context 组装链",
                                   .updated_at = 2000,
                                   .ttl_ms = 0,
                                   .source = "agent",
                               });

  dasall::memory::VectorConfig vector_config;
  vector_config.enabled = true;
  vector_config.search_top_k = 5;
  dasall::memory::MemoryConfig config;
  config.context.recent_turn_limit = 1;
  config.context.fact_confidence_floor = 80;
  config.vector = vector_config;

  dasall::memory::UnavailableVectorMemoryIndexAdapter unavailable_vector(config.vector);
  dasall::memory::CandidateCollector collector(*board, store, config, &unavailable_vector);

  const auto set = collector.collect(dasall::memory::CandidateCollectRequest{
      .session_id = "session-016",
      .stage = "plan",
      .goal_summary = "完成 Context 组装链",
      .external_evidence = std::vector<std::string>{"docs-evidence-016"},
  });

  assert_equal(2, set.session_bundle.total_turn_count,
               "candidate collector should keep session context even when one query source fails");
  assert_equal(1, static_cast<int>(set.relevant_facts.size()),
               "candidate collector should keep surviving facts when experience query degrades");
  assert_true(set.relevant_experiences.empty(),
              "candidate collector should downgrade failed experience queries to an empty result set");
  assert_true(set.vector_hits.empty(),
              "candidate collector should downgrade unavailable vector backends to an empty result set");
  assert_true(contains_warning(set.warnings, "experience_query_unavailable"),
              "candidate collector should emit a warning when experience query fails independently");
  assert_true(contains_warning(set.warnings, "vector_unavailable"),
              "candidate collector should emit a warning when vector is enabled but unavailable");
}

}  // namespace

int main() {
  try {
    test_candidate_collector_collects_multi_source_candidates_and_estimates_tokens();
    test_candidate_collector_degrades_when_experience_query_fails_and_vector_is_unavailable();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}