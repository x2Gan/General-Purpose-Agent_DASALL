#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef DASALL_SQL_MEMORY_DIR
#define DASALL_SQL_MEMORY_DIR ""
#endif

#include "IMemoryManager.h"
#include "store/sqlite/SqliteMemoryStore.h"
#include "support/TestAssertions.h"

namespace {

std::filesystem::path make_temp_database_path() {
  const auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
  return std::filesystem::temp_directory_path() /
         ("dasall-memory-writeback-integration-" + std::to_string(timestamp) +
          ".db");
}

[[nodiscard]] dasall::memory::MemoryWritebackRequest make_request(
    const std::string& session_id,
    const std::string& turn_id,
    const std::string& user_input,
    const std::string& agent_response,
    const std::string& fact_text,
    std::uint32_t fact_confidence,
    bool include_experience) {
  dasall::memory::MemoryWritebackRequest request;
  request.session_id = session_id;
  request.turn.turn_id = turn_id;
  request.turn.session_id = session_id;
  request.turn.user_input = user_input;
  request.turn.agent_response = agent_response;
  request.turn.created_at = 2000 + static_cast<std::int64_t>(fact_confidence);
  request.summary_candidate = dasall::contracts::SummaryMemory{};
  request.summary_candidate->summary_text = std::string{"summary for "} + turn_id;
  request.summary_candidate->decisions_made =
      std::vector<std::string>{std::string{"decision for "} + turn_id};
  request.summary_candidate->confirmed_facts = std::vector<std::string>{fact_text};
    dasall::memory::FactCandidate fact_candidate;
    fact_candidate.fact.fact_text = fact_text;
    fact_candidate.fact.source_turn_ids = std::vector<std::string>{turn_id};
    fact_candidate.fact.confidence_score = fact_confidence;
    fact_candidate.fact.created_at =
      2000 + static_cast<std::int64_t>(fact_confidence);
    fact_candidate.fact.fact_type = "status";
    fact_candidate.extraction_source = "turn";
    request.fact_candidates.push_back(std::move(fact_candidate));

  if (include_experience) {
    dasall::memory::ExperienceCandidate experience_candidate;
    experience_candidate.experience.lesson_summary =
      "冲突后应保留 supersede 关系";
    experience_candidate.experience.trigger_condition =
      "fact conflict detected";
    experience_candidate.experience.recommended_action =
      "record supersede and continue";
    experience_candidate.experience.created_at = 3000;
    experience_candidate.extraction_source = "reflection";
    request.experience_candidates.push_back(std::move(experience_candidate));
  }

  return request;
}

[[nodiscard]] bool snapshot_has_slot(
    const dasall::memory::WorkingMemorySnapshot& snapshot,
    const std::string& key,
    const std::string& expected_value) {
  return std::any_of(snapshot.slots.begin(), snapshot.slots.end(),
                     [&key, &expected_value](const dasall::memory::WorkingMemorySlot& slot) {
                       return slot.key == key && slot.value == expected_value;
                     });
}

void test_memory_manager_writeback_integration_persists_and_supersedes_facts() {
  using dasall::tests::support::assert_true;

  dasall::memory::MemoryConfig config;
  config.storage.backend = dasall::memory::StorageBackend::Sqlite;
  config.storage.db_path = make_temp_database_path().string();
  config.storage.migrations_dir = DASALL_SQL_MEMORY_DIR;
  if (config.storage.migrations_dir.empty()) {
    throw std::runtime_error("DASALL_SQL_MEMORY_DIR must be defined for memory writeback integration coverage");
  }

  auto manager = dasall::memory::create_memory_manager(config);
  const auto init_code = manager->init(config);
  assert_true(static_cast<int>(init_code) == 0,
              "sqlite-backed memory manager should initialize before integration writeback");

  const auto first_result = manager->write_back(make_request(
      "session-021-integration", "turn-021-001", "记住当前网络状态",
      "第一次写回 network enabled", "network mode enabled", 70, false));
  assert_true(!first_result.result_code.has_value() && first_result.fact_ids.size() == 1U &&
                  first_result.summary_id.has_value(),
              "first writeback should persist the initial turn, fact and summary");

  const auto second_result = manager->write_back(make_request(
      "session-021-integration", "turn-021-002", "更新网络状态",
      "第二次写回 network disabled", "network mode disabled", 95, true));
  assert_true(!second_result.result_code.has_value(),
              "second writeback should stay successful on the supersede path");
  assert_true(second_result.conflicts.size() == 1U &&
                  second_result.conflicts.front().action ==
                      dasall::memory::ConflictAction::Supersede,
              "second writeback should surface one supersede conflict record");
  assert_true(second_result.fact_ids.size() == 1U &&
                  second_result.experience_ids.size() == 1U &&
                  second_result.summary_id.has_value(),
              "second writeback should persist the new fact, one experience and the refreshed summary");

  const auto export_result = manager->export_working_memory_snapshot(
      dasall::memory::WorkingMemoryExportRequest{
          .session_id = "session-021-integration",
          .export_reason = "integration-verify",
          .include_ephemeral_facts = true,
      });
  assert_true(!export_result.result_code.has_value() &&
                  snapshot_has_slot(export_result.snapshot, "latest_turn_id", "turn-021-002") &&
                  snapshot_has_slot(export_result.snapshot, "latest_summary_id",
                                    *second_result.summary_id),
              "writeback integration should update the working board through MemoryManager export");

  auto store = dasall::memory::store::sqlite::create_sqlite_memory_store();
  const auto open_result = store->open(config);
  if (open_result.has_value()) {
    throw std::runtime_error("failed to reopen sqlite store for integration verification");
  }

  const auto bundle = store->load_session_bundle(
      dasall::memory::SessionLoadRequest{.session_id = "session-021-integration",
                                         .recent_turn_limit = 8});
  assert_true(bundle.total_turn_count == 2 && bundle.recent_turns.size() == 2U,
              "writeback integration should persist both turns in the session timeline");

  const auto summary = store->load_latest_summary("session-021-integration");
  assert_true(summary.has_value() && summary->summary_id == second_result.summary_id,
              "writeback integration should persist the latest summary from the second turn");

  dasall::memory::FactQuery fact_query;
  fact_query.session_id = "session-021-integration";
  fact_query.exclude_superseded = false;
  const auto fact_result = store->query_facts(fact_query);
  assert_true(fact_result.total_count == 2,
              "writeback integration should leave both the original and superseding facts queryable when superseded rows are included");

  const auto superseded_it = std::find_if(
      fact_result.facts.begin(), fact_result.facts.end(),
      [&second_result](const dasall::contracts::MemoryFact& fact) {
        return fact.superseded_by_fact_id ==
               std::optional<std::string>{second_result.fact_ids.front()};
      });
  assert_true(superseded_it != fact_result.facts.end(),
              "writeback integration should record the supersede relation on the original fact");

  dasall::memory::ExperienceQuery experience_query;
  experience_query.session_id = "session-021-integration";
  experience_query.exclude_expired = false;
  const auto experience_result = store->query_experiences(experience_query);
  assert_true(experience_result.total_count == 1,
              "writeback integration should persist the derived experience from the second turn");

  store->close();
  manager->shutdown();
}

}  // namespace

int main() {
  try {
    test_memory_manager_writeback_integration_persists_and_supersedes_facts();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}