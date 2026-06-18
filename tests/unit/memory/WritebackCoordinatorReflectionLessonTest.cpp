#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

#ifndef DASALL_SQL_MEMORY_DIR
#error DASALL_SQL_MEMORY_DIR must be defined for reflection lesson writeback coverage
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
         ("dasall-writeback-reflection-lesson-" + std::to_string(timestamp) +
          ".db");
}

void cleanup_database_artifacts(const std::filesystem::path& database_path) {
  (void)std::filesystem::remove(database_path);
  (void)std::filesystem::remove(database_path.string() + "-wal");
  (void)std::filesystem::remove(database_path.string() + "-shm");
}

[[nodiscard]] dasall::memory::MemoryConfig make_sqlite_config(
    const std::filesystem::path& database_path) {
  dasall::memory::MemoryConfig config;
  config.storage.backend = dasall::memory::StorageBackend::Sqlite;
  config.storage.db_path = database_path.string();
  config.storage.migrations_dir = DASALL_SQL_MEMORY_DIR;
  config.vector.enabled = false;
  return config;
}

[[nodiscard]] dasall::memory::MemoryWritebackRequest make_request(
    const std::string& session_id,
    const std::string& turn_id) {
  dasall::memory::MemoryWritebackRequest request;
  request.request_id = "req-writeback-reflection-lesson";
  request.session_id = session_id;
  request.trace_id = "trace-writeback-reflection-lesson";
  request.turn.turn_id = turn_id;
  request.turn.session_id = session_id;
  request.turn.user_input = "dataset query timed out while collecting evidence";
  request.turn.agent_response = "reflection lesson should be persisted as an experience";
  request.turn.created_at = 1712746800100LL;
  request.reflection_lesson = dasall::contracts::ReflectionLessonProjection{
      .lesson_summary =
          "When the dataset query times out, refresh the retrieved evidence before retrying the step",
      .trigger_condition =
          "dataset query timed out while collecting governed evidence",
      .recommended_action =
          "retry the failed step after refreshing the retrieved evidence",
      .effectiveness_score = 87U,
      .applicable_domains = std::vector<std::string>{"reflection"},
      .tags = std::vector<std::string>{"diagnostic:reflection-timeout"},
  };
  return request;
}

void test_writeback_coordinator_projects_reflection_lesson_into_experience_memory() {
  using dasall::tests::support::assert_true;

  const auto database_path = make_temp_database_path();
  cleanup_database_artifacts(database_path);

  const auto config = make_sqlite_config(database_path);
  auto manager = dasall::memory::create_memory_manager(config);
  const auto init_code = manager->init(config);
  assert_true(static_cast<int>(init_code) == 0,
              "reflection lesson writeback test requires sqlite memory manager init");

  const auto writeback_result = manager->write_back(
      make_request("session-writeback-reflection-lesson", "turn-reflection-lesson-001"));
  assert_true(!writeback_result.result_code.has_value(),
              "reflection lesson writeback should stay on the success path");
  assert_true(writeback_result.experience_ids.size() == 1U,
              "reflection lesson writeback should persist one derived experience");

  auto store = dasall::memory::store::sqlite::create_sqlite_memory_store();
  const auto open_result = store->open(config);
  if (open_result.has_value()) {
    throw std::runtime_error(
        "failed to reopen sqlite store for reflection lesson verification");
  }

  dasall::memory::ExperienceQuery experience_query;
  experience_query.session_id = "session-writeback-reflection-lesson";
  experience_query.stage = "reflection";
  experience_query.exclude_expired = false;
  const auto experience_result = store->query_experiences(experience_query);
  assert_true(experience_result.total_count == 1,
              "reflection stage recall should return the synthesized self-reflection experience");

  const auto& experience = experience_result.experiences.front();
  assert_true(
      experience.lesson_summary ==
          std::optional<std::string>{
              "When the dataset query times out, refresh the retrieved evidence before retrying the step"},
      "reflection lesson writeback should preserve the lesson summary");
  assert_true(
      experience.recommended_action ==
          std::optional<std::string>{
              "retry the failed step after refreshing the retrieved evidence"},
      "reflection lesson writeback should preserve the recommended action");
  assert_true(experience.tags.has_value() &&
                  std::find(experience.tags->begin(),
                            experience.tags->end(),
                            std::string{"experience_kind:self_reflection"}) !=
                      experience.tags->end(),
              "reflection lesson writeback should stamp the self_reflection audit tag");
  assert_true(experience.tags.has_value() &&
                  std::find(experience.tags->begin(),
                            experience.tags->end(),
                            std::string{"stage:reflection"}) !=
                      experience.tags->end(),
              "reflection lesson writeback should keep the reflection stage tag for later recall");

  store->close();
  manager->shutdown();
  cleanup_database_artifacts(database_path);
}

}  // namespace

int main() {
  try {
    test_writeback_coordinator_projects_reflection_lesson_into_experience_memory();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}
