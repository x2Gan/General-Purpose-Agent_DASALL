#include <algorithm>
#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "FakeMemoryStore.h"
#include "memory/Session.h"
#include "memory/SummaryMemory.h"
#include "support/TestAssertions.h"
#include "writeback/CompressionCoordinator.h"

namespace {

bool contains_value(const std::vector<std::string>& values,
                    const std::string& expected_fragment) {
  return std::any_of(values.begin(), values.end(),
                     [&expected_fragment](const std::string& value) {
                       return value.find(expected_fragment) != std::string::npos;
                     });
}

dasall::contracts::Turn make_turn(const std::string& turn_id,
                                  const std::string& session_id,
                                  const std::string& user_input,
                                  const std::string& agent_response,
                                  std::vector<std::string> tool_refs = {},
                                  std::vector<std::string> observation_refs = {}) {
  dasall::contracts::Turn turn;
  turn.turn_id = turn_id;
  turn.session_id = session_id;
  turn.user_input = user_input;
  turn.agent_response = agent_response;
  if (!tool_refs.empty()) {
    turn.tool_call_refs = std::move(tool_refs);
  }
  if (!observation_refs.empty()) {
    turn.observation_refs = std::move(observation_refs);
  }
  turn.created_at = 1000;
  return turn;
}

void seed_session(dasall::tests::mocks::FakeMemoryStore& store,
                  const std::string& session_id) {
  dasall::contracts::Session session;
  session.session_id = session_id;
  session.turn_ids = std::vector<std::string>{};
  session.created_at = 900;
  if (!store.create_session(session).ok) {
    throw std::runtime_error("failed to seed compression session");
  }
}

void test_compression_coordinator_uses_template_path_and_materializes_summary() {
  using dasall::tests::support::assert_true;

  dasall::tests::mocks::FakeMemoryStore store;
  seed_session(store, "session-018");

  dasall::memory::CompressionCoordinator coordinator(store);
  const auto output = coordinator.compress(dasall::memory::CompressionInput{
      .session_id = "session-018",
      .source_turns = {
          make_turn("turn-018-001", "session-018", "请确认 sqlite store 已就绪。",
                    "决定先实现 CandidateCollector。", {"tool-1"}, {"obs-1"}),
          make_turn("turn-018-002", "session-018", "下一步需要预算裁剪。",
                    "已完成多源候选收集，计划继续预算裁剪。"),
      },
      .existing_summary = std::nullopt,
      .target_token_budget = 256,
      .materialize_latest_summary = true,
      .strategy_hint = "template",
  });

  assert_true(output.compression_applied,
              "template compression should report that compression was applied when turns exist");
  assert_true(!output.projection.summary_text.empty(),
              "template compression should generate summary text");
  assert_true(contains_value(output.projection.decisions_made, "决定先实现 CandidateCollector"),
              "template compression should extract Chinese decision phrases from turn responses");
  assert_true(contains_value(output.projection.confirmed_facts, "确认 sqlite store 已就绪"),
              "template compression should preserve Chinese confirmation phrases as facts");
  assert_true(contains_value(output.projection.tool_outcomes, "tool_calls: tool-1"),
              "template compression should extract tool outcome summaries from tool refs");
  assert_true(dasall::contracts::validate_summary_memory_field_rules(output.summary).ok,
              "template compression should materialize a SummaryMemory that satisfies the frozen contract guard");
  assert_true(contains_value(output.compression_notes, "summary_materialized"),
              "template compression should report materialization when latest summary is persisted");

  const auto latest_summary = store.load_latest_summary("session-018");
  assert_true(latest_summary.has_value(),
              "template compression should persist the latest summary when materialization is requested");
  assert_true(latest_summary->summary_id == output.summary.summary_id,
              "persisted summary id should match the materialized summary output");
}

void test_compression_coordinator_merges_existing_summary_without_duplicates() {
  using dasall::tests::support::assert_true;

  dasall::tests::mocks::FakeMemoryStore store;
  dasall::memory::CompressionCoordinator coordinator(store);

  dasall::contracts::SummaryMemory existing_summary;
  existing_summary.summary_id = "summary-018-existing";
  existing_summary.session_id = "session-018-merge";
  existing_summary.summary_text = "旧摘要";
  existing_summary.source_turn_ids = std::vector<std::string>{"turn-018-000"};
  existing_summary.decisions_made = std::vector<std::string>{"保留旧决策"};
  existing_summary.confirmed_facts = std::vector<std::string>{"确认旧事实"};
  existing_summary.tool_outcomes = std::vector<std::string>{"tool_calls: tool-1"};
  existing_summary.created_at = 800;

  const auto output = coordinator.compress(dasall::memory::CompressionInput{
      .session_id = "session-018-merge",
      .source_turns = {
          make_turn("turn-018-001", "session-018-merge", "确认 sqlite store 已就绪。",
                    "计划继续预算裁剪。", {"tool-1"}),
      },
      .existing_summary = existing_summary,
      .target_token_budget = 128,
      .strategy_hint = "template",
  });

  const auto tool_outcome_count = static_cast<int>(std::count(
      output.projection.tool_outcomes.begin(), output.projection.tool_outcomes.end(),
      std::string{"tool_calls: tool-1"}));

  assert_true(output.summary.summary_id == std::optional<std::string>{"summary-018-existing"},
              "existing summary ids should be reused during incremental merge");
  assert_true(output.projection.summary_text.find("旧摘要") != std::string::npos,
              "incremental merge should retain the existing summary text");
  assert_true(output.projection.summary_text.find("计划继续预算裁剪") != std::string::npos,
              "incremental merge should append the new template summary text");
  assert_true(contains_value(output.projection.decisions_made, "保留旧决策"),
              "incremental merge should preserve existing structured decisions");
  assert_true(contains_value(output.projection.decisions_made, "计划继续预算裁剪"),
              "incremental merge should add newly extracted decisions");
  assert_true(tool_outcome_count == 1,
              "incremental merge should deduplicate repeated tool outcome entries");
}

}  // namespace

int main() {
  try {
    test_compression_coordinator_uses_template_path_and_materializes_summary();
    test_compression_coordinator_merges_existing_summary_without_duplicates();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}