#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "FakeMemoryStore.h"
#include "memory/Session.h"
#include "support/TestAssertions.h"
#include "writeback/HierarchicalSummarizationCoordinator.h"

namespace {

void seed_session(dasall::tests::mocks::FakeMemoryStore& store,
                  const std::string& session_id,
                  const std::string& user_id) {
  dasall::contracts::Session session;
  session.session_id = session_id;
  session.turn_ids = std::vector<std::string>{};
  session.user_id = user_id;
  session.created_at = 100;
  if (!store.create_session(session).ok) {
    throw std::runtime_error("failed to seed hierarchy session");
  }
}

dasall::contracts::SummaryMemory make_summary(
    const std::string& summary_id,
    const std::string& session_id,
    dasall::memory::HierarchicalSummaryLevel level,
    std::int64_t created_at) {
  dasall::contracts::SummaryMemory summary;
  summary.summary_id = summary_id;
  summary.session_id = session_id;
  summary.summary_text = std::string{"summary body for "} + summary_id;
  summary.source_turn_ids = std::vector<std::string>{summary_id + "-turn"};
  summary.decisions_made = std::vector<std::string>{"decision-" + summary_id};
  summary.confirmed_facts = std::vector<std::string>{"fact-" + summary_id};
  summary.tool_outcomes = std::vector<std::string>{"tool-" + summary_id};
  summary.created_at = created_at;
  summary.tags = std::vector<std::string>{"compression"};
  dasall::memory::ensure_summary_level_tag(summary, level);
  return summary;
}

void test_hierarchical_summarization_promotes_dialog_summaries_to_topic_page() {
  using dasall::tests::support::assert_true;

  dasall::memory::MemoryConfig config;
  config.compression.hierarchy.enabled = true;
  config.compression.hierarchy.dialog_to_topic_threshold = 2;
  config.compression.hierarchy.topic_to_profile_threshold = 2;

  dasall::tests::mocks::FakeMemoryStore store;
  seed_session(store, "session-hierarchy-001", "user-hierarchy-001");

  auto first_summary = make_summary("dialog-001", "session-hierarchy-001",
                                    dasall::memory::HierarchicalSummaryLevel::Dialog, 1000);
  auto second_summary = make_summary("dialog-002", "session-hierarchy-001",
                                     dasall::memory::HierarchicalSummaryLevel::Dialog, 1010);

  assert_true(store.upsert_summary(first_summary).ok,
              "hierarchy test should seed the first dialog summary");
  assert_true(store.upsert_summary(second_summary).ok,
              "hierarchy test should seed the second dialog summary");

  dasall::memory::HierarchicalSummarizationCoordinator coordinator(
      store, store, config);
  const auto result = coordinator.promote_from_summary(second_summary);

  assert_true(result.promoted,
              "hierarchy coordinator should promote when dialog threshold is reached");
  assert_true(result.promoted_summary.has_value(),
              "hierarchy coordinator should materialize a promoted parent summary");
  assert_true(dasall::memory::summary_matches_level(
                  *result.promoted_summary,
                  dasall::memory::HierarchicalSummaryLevel::Topic),
              "hierarchy coordinator should tag the promoted page as topic level");
  assert_true(result.promoted_summary->source_turn_ids.has_value() &&
                  result.promoted_summary->source_turn_ids->size() == 2U,
              "hierarchy coordinator should carry child summary ids into the topic page provenance");

  const auto latest_topic_summary = store.load_latest_summary(
      "session-hierarchy-001", dasall::memory::HierarchicalSummaryLevel::Topic);
  assert_true(latest_topic_summary.has_value(),
              "topic-level latest summary should be queryable after promotion");

  const auto remaining_dialog_summaries = store.load_unparented_summaries(
      "session-hierarchy-001", dasall::memory::HierarchicalSummaryLevel::Dialog, 8);
  assert_true(remaining_dialog_summaries.empty(),
              "promoted child dialog summaries should no longer appear as unparented dialog pages");
}

void test_hierarchical_summarization_requires_threshold_before_promoting() {
  using dasall::tests::support::assert_true;

  dasall::memory::MemoryConfig config;
  config.compression.hierarchy.enabled = true;
  config.compression.hierarchy.dialog_to_topic_threshold = 3;

  dasall::tests::mocks::FakeMemoryStore store;
  seed_session(store, "session-hierarchy-002", "user-hierarchy-002");

  auto only_summary = make_summary("dialog-101", "session-hierarchy-002",
                                   dasall::memory::HierarchicalSummaryLevel::Dialog, 2000);
  assert_true(store.upsert_summary(only_summary).ok,
              "hierarchy threshold test should seed a single dialog summary");

  dasall::memory::HierarchicalSummarizationCoordinator coordinator(
      store, store, config);
  const auto result = coordinator.promote_from_summary(only_summary);

  assert_true(!result.promoted,
              "hierarchy coordinator should not promote before the configured threshold is reached");
  assert_true(!store.load_latest_summary(
                   "session-hierarchy-002",
                   dasall::memory::HierarchicalSummaryLevel::Topic)
                   .has_value(),
              "no topic-level page should be materialized below the threshold");
}

}  // namespace

int main() {
  try {
    test_hierarchical_summarization_promotes_dialog_summaries_to_topic_page();
    test_hierarchical_summarization_requires_threshold_before_promoting();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}