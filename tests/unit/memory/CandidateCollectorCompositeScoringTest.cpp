#include <chrono>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "FakeMemoryStore.h"
#include "config/MemoryConfig.h"
#include "context/CandidateCollector.h"
#include "support/TestAssertions.h"
#include "working/IWorkingMemoryBoard.h"

namespace {

std::int64_t current_time_millis() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

dasall::memory::MemoryConfig make_memory_config(bool composite_enabled) {
  dasall::memory::MemoryConfig config;
  config.context.recent_turn_limit = 2;
  config.context.fact_confidence_floor = 0;
  config.context.scoring.composite_enabled = composite_enabled;
  config.context.scoring.confidence_weight = 0.35;
  config.context.scoring.recency_weight = 0.30;
  config.context.scoring.hit_rate_weight = 0.20;
  config.context.scoring.source_weight = 0.15;
  config.vector.enabled = false;
  return config;
}

void seed_session_and_turn(dasall::tests::mocks::FakeMemoryStore& store,
                           std::int64_t now_millis) {
  dasall::contracts::Session session;
  session.session_id = "session-composite-scoring";
  session.turn_ids = std::vector<std::string>{};
  session.user_id = "user-composite-scoring";
  session.created_at = now_millis - 20000;
  session.last_active_at = now_millis - 10000;
  if (!store.create_session(session).ok) {
    throw std::runtime_error("failed to seed composite scoring session");
  }

  dasall::contracts::Turn turn;
  turn.turn_id = "turn-composite-scoring";
  turn.session_id = "session-composite-scoring";
  turn.user_input = "验证 composite scoring 排序";
  turn.created_at = now_millis - 15000;
  if (!store.append_turn(turn).ok) {
    throw std::runtime_error("failed to seed composite scoring turn");
  }
}

void seed_fact_candidates(dasall::tests::mocks::FakeMemoryStore& store,
                          std::int64_t now_millis) {
  dasall::contracts::MemoryFact confidence_fact;
  confidence_fact.fact_id = "fact-confidence";
  confidence_fact.session_id = "session-composite-scoring";
  confidence_fact.fact_text = "高置信度但来源稀疏的事实。";
  confidence_fact.source_turn_ids = std::vector<std::string>{"turn-composite-scoring"};
  confidence_fact.confidence_score = 95;
  confidence_fact.created_at = now_millis - 9000;
  confidence_fact.fact_type = "constraint";
  if (!store.insert_fact(confidence_fact).ok) {
    throw std::runtime_error("failed to seed confidence-dominant fact");
  }

  dasall::contracts::MemoryFact composite_fact;
  composite_fact.fact_id = "fact-composite";
  composite_fact.session_id = "session-composite-scoring";
  composite_fact.fact_text = "近期高频且证据完整的事实应该在 composite scoring 下优先。";
  composite_fact.source_turn_ids = std::vector<std::string>{"turn-composite-scoring"};
  composite_fact.confidence_score = 70;
  composite_fact.created_at = now_millis - 8000;
  composite_fact.fact_type = "constraint";
  composite_fact.source_observation_refs = std::vector<std::string>{"obs-composite"};
  composite_fact.evidence_digest = "digest-composite";
  composite_fact.tags = std::vector<std::string>{"preferred"};
  if (!store.insert_fact(composite_fact).ok) {
    throw std::runtime_error("failed to seed composite-dominant fact");
  }

  for (int count = 0; count < 4; ++count) {
    if (!store.touch_facts({"fact-composite"}, now_millis - 10).ok) {
      throw std::runtime_error("failed to warm composite-dominant fact");
    }
  }
}

void seed_experience_candidates(dasall::tests::mocks::FakeMemoryStore& store,
                                std::int64_t now_millis) {
  dasall::contracts::ExperienceMemory confidence_experience;
  confidence_experience.experience_id = "experience-confidence";
  confidence_experience.session_id = "session-composite-scoring";
  confidence_experience.lesson_summary = "高 effectiveness 但证据稀疏的经验。";
  confidence_experience.trigger_condition = "stage=plan";
  confidence_experience.recommended_action = "prefer confidence only";
  confidence_experience.created_at = now_millis - 9000;
  confidence_experience.effectiveness_score = 92;
  confidence_experience.tags = std::vector<std::string>{"stage:plan"};
  if (!store.insert_experience(confidence_experience).ok) {
    throw std::runtime_error("failed to seed confidence-dominant experience");
  }

  dasall::contracts::ExperienceMemory composite_experience;
  composite_experience.experience_id = "experience-composite";
  composite_experience.session_id = "session-composite-scoring";
  composite_experience.lesson_summary = "近期高频且来源完整的经验应该在 composite scoring 下优先。";
  composite_experience.trigger_condition = "stage=plan";
  composite_experience.recommended_action = "prefer composite scoring";
  composite_experience.created_at = now_millis - 8000;
  composite_experience.effectiveness_score = 60;
  composite_experience.source_fact_ids = std::vector<std::string>{"fact-composite"};
  composite_experience.source_turn_ids = std::vector<std::string>{"turn-composite-scoring"};
  composite_experience.applicable_domains = std::vector<std::string>{"memory"};
  composite_experience.risk_notes = "validated via repeated retrieval";
  composite_experience.tags = std::vector<std::string>{"stage:plan", "preferred"};
  if (!store.insert_experience(composite_experience).ok) {
    throw std::runtime_error("failed to seed composite-dominant experience");
  }

  for (int count = 0; count < 4; ++count) {
    if (!store.touch_experiences({"experience-composite"}, now_millis - 10).ok) {
      throw std::runtime_error("failed to warm composite-dominant experience");
    }
  }
}

dasall::memory::CandidateSet collect_candidates(bool composite_enabled) {
  dasall::tests::mocks::FakeMemoryStore store;
  const auto now_millis = current_time_millis();
  seed_session_and_turn(store, now_millis);
  seed_fact_candidates(store, now_millis);
  seed_experience_candidates(store, now_millis);

  auto board = dasall::memory::create_working_memory_board();
  dasall::memory::CandidateCollector collector(
      *board,
      store,
      store,
      store,
      store,
      make_memory_config(composite_enabled),
      nullptr);

  return collector.collect(dasall::memory::CandidateCollectRequest{
      .session_id = "session-composite-scoring",
      .stage = "plan",
      .goal_summary = "验证 collector composite scoring",
      .token_budget_hint = 512,
      .latency_budget_ms = 100,
      .external_evidence = {},
  });
}

void test_candidate_collector_prefers_recent_frequent_and_well_sourced_candidates() {
  using dasall::tests::support::assert_equal;

  const auto set = collect_candidates(true);

  assert_equal(std::string{"fact-composite"},
               set.relevant_facts.front().fact_id.value_or(std::string{}),
               "composite scoring should let recency, hit-rate and provenance outrank raw confidence for facts");
  assert_equal(std::string{"experience-composite"},
               set.relevant_experiences.front().experience_id.value_or(std::string{}),
               "composite scoring should let recency, hit-rate and provenance outrank raw effectiveness for experiences");
}

void test_candidate_collector_can_fall_back_to_confidence_only_ordering() {
  using dasall::tests::support::assert_equal;

  const auto set = collect_candidates(false);

  assert_equal(std::string{"fact-confidence"},
               set.relevant_facts.front().fact_id.value_or(std::string{}),
               "confidence-only fallback should restore the legacy fact ordering");
  assert_equal(std::string{"experience-confidence"},
               set.relevant_experiences.front().experience_id.value_or(std::string{}),
               "confidence-only fallback should restore the legacy experience ordering");
}

}  // namespace

int main() {
  try {
    test_candidate_collector_prefers_recent_frequent_and_well_sourced_candidates();
    test_candidate_collector_can_fall_back_to_confidence_only_ordering();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}