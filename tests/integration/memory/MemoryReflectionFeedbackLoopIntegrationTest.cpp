#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

#ifndef DASALL_SQL_MEMORY_DIR
#error DASALL_SQL_MEMORY_DIR must be defined for reflection feedback loop coverage
#endif

#include "IMemoryManager.h"
#include "ReflectionLessonProjector.h"
#include "support/TestAssertions.h"
#include "MockCognitionFixture.h"

namespace {

std::filesystem::path make_temp_database_path() {
  const auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
  return std::filesystem::temp_directory_path() /
         ("dasall-memory-reflection-feedback-loop-" +
          std::to_string(timestamp) + ".db");
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
  config.context.compression_trigger_turns = 8;
  config.vector.enabled = false;
  return config;
}

[[nodiscard]] dasall::contracts::Observation make_failed_observation(
    const dasall::tests::mocks::MockCognitionFixture& fixture) {
  auto failed_observation = fixture.make_observation(
      false,
      "dataset request timed out while collecting the governed evidence");
  failed_observation.error = dasall::contracts::ErrorInfo{
      .failure_type = dasall::contracts::ResultCodeCategory::Tool,
      .retryable = true,
      .safe_to_replan = true,
      .details = {.code = 408,
                  .message =
                      "dataset request timed out while collecting the governed evidence",
                  .stage = "tool_execution"},
      .source_ref = {.ref_type = "observation", .ref_id = "obs-cognition-fixture"},
  };
  return failed_observation;
}

[[nodiscard]] dasall::contracts::AgentRequest make_agent_request(
    const dasall::tests::mocks::MockCognitionFixtureOptions& options,
    const std::string& session_id) {
  dasall::contracts::AgentRequest request;
  request.request_id = options.request_id;
  request.session_id = session_id;
  request.trace_id = options.trace_id;
  request.user_input = options.user_turn;
  request.request_channel = dasall::contracts::RequestChannel::Daemon;
  request.created_at = options.base_timestamp_ms;
  request.goal_hint = options.goal_summary;
  return request;
}

void test_memory_reflection_feedback_loop_recalls_prior_lesson_into_context_packet() {
  using dasall::tests::support::assert_true;

  const auto database_path = make_temp_database_path();
  cleanup_database_artifacts(database_path);

  const auto config = make_sqlite_config(database_path);
  auto manager = dasall::memory::create_memory_manager(config);
  const auto init_code = manager->init(config);
  assert_true(static_cast<int>(init_code) == 0,
              "reflection feedback loop integration requires sqlite memory manager init");

  dasall::tests::mocks::MockCognitionFixture fixture(
      dasall::tests::mocks::MockCognitionFixtureOptions{
          .request_id = "req-memory-reflection-feedback",
          .trace_id = "trace-memory-reflection-feedback",
          .goal_id = "goal-memory-reflection-feedback",
          .user_turn = "retry the governed dataset request safely",
          .goal_summary = "stabilize the governed dataset retrieval path",
      });
  auto engine = fixture.make_engine();
  const auto failed_observation = make_failed_observation(fixture);
  const auto reflection_result =
      engine->reflect(fixture.make_reflection_request(failed_observation));

  assert_true(reflection_result.reflection_decision.has_value(),
              "reflection feedback loop integration requires a reflection decision");
  assert_true(reflection_result.reflection_lesson.has_value(),
              "reflection feedback loop integration requires cognition to surface a reusable lesson projection");

  const auto writeback_request =
      dasall::runtime::ReflectionLessonProjector{}.make_writeback_request(
          make_agent_request(fixture.options(), "session-memory-reflection-feedback"),
          fixture.options().goal_id,
          failed_observation,
          reflection_result);
  assert_true(writeback_request.has_value(),
              "runtime reflection lesson projector should build a writeback request when cognition emitted a lesson");

  const auto writeback_result = manager->write_back(*writeback_request);
  assert_true(!writeback_result.result_code.has_value() &&
                  writeback_result.experience_ids.size() == 1U,
              "reflection lesson writeback should persist one experience before the next context assembly");

  const auto context_result = manager->prepare_context(
      dasall::memory::MemoryContextRequest{
          .request_id = "req-memory-reflection-feedback-context",
          .session_id = "session-memory-reflection-feedback",
          .trace_id = "trace-memory-reflection-feedback-context",
          .stage = "reflection",
          .user_turn = fixture.options().user_turn,
          .goal_summary = fixture.options().goal_summary,
          .constraints_summary = "carry forward reusable reflection lessons before retrying",
          .latest_observation_digest_summary =
              "dataset request timed out while collecting the governed evidence",
          .visible_tools = {"agent.dataset"},
          .token_budget_hint = 512,
          .latency_budget_ms = 100,
          .external_evidence = {},
          .retrieval_evidence_refs = {},
      });

  assert_true(!context_result.result_code.has_value(),
              "reflection feedback loop integration should assemble context successfully after lesson writeback");
  assert_true(context_result.context_packet.retrieval_evidence.has_value(),
              "reflection feedback loop integration should surface recalled lessons through retrieval_evidence");
  assert_true(
      std::any_of(context_result.context_packet.retrieval_evidence->begin(),
                  context_result.context_packet.retrieval_evidence->end(),
                  [](const std::string& entry) {
                    return entry.find("[experience]") != std::string::npos &&
                           entry.find("dataset request timed out") != std::string::npos &&
                           entry.find("retry the failed step") != std::string::npos;
                  }),
      "next-round context should recall the persisted reflection lesson and project it into ContextPacket");

  manager->shutdown();
  cleanup_database_artifacts(database_path);
}

}  // namespace

int main() {
  try {
    test_memory_reflection_feedback_loop_recalls_prior_lesson_into_context_packet();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}
