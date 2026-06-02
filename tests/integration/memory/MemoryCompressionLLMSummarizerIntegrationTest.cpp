#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#ifndef DASALL_SQL_MEMORY_DIR
#error DASALL_SQL_MEMORY_DIR must be defined for memory compression LLM summarizer integration coverage
#endif

#include "IMemoryManager.h"
#include "LLMBackedSummarizer.h"
#include "MockLLMManager.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::mocks::MockLLMManager;

[[nodiscard]] std::filesystem::path make_temp_database_path() {
  const auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
  return std::filesystem::temp_directory_path() /
         ("dasall-memory-llm-summarizer-integration-" +
          std::to_string(timestamp) + ".db");
}

void cleanup_database_artifacts(const std::filesystem::path& database_path) {
  (void)std::filesystem::remove(database_path);
  (void)std::filesystem::remove(database_path.string() + "-wal");
  (void)std::filesystem::remove(database_path.string() + "-shm");
}

[[nodiscard]] bool contains_value(const std::vector<std::string>& values,
                                  std::string_view expected_fragment) {
  for (const auto& value : values) {
    if (value.find(expected_fragment) != std::string::npos) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] dasall::memory::MemoryConfig make_sqlite_config(
    const std::filesystem::path& database_path) {
  dasall::memory::MemoryConfig config;
  config.storage.backend = dasall::memory::StorageBackend::Sqlite;
  config.storage.db_path = database_path.string();
  config.storage.reader_pool_size = 1;
  config.storage.migrations_dir = DASALL_SQL_MEMORY_DIR;
  config.context.compression_trigger_turns = 2;
  config.context.compression_trigger_ratio = 0.5;
  config.vector.enabled = false;
  return config;
}

[[nodiscard]] dasall::memory::MemoryWritebackRequest make_request(
    const std::string& session_id,
    const std::string& turn_id,
    const std::string& user_input,
    const std::string& agent_response,
    const std::string& fact_text,
    std::uint32_t confidence_score) {
  dasall::memory::MemoryWritebackRequest request;
  request.session_id = session_id;
  request.turn.turn_id = turn_id;
  request.turn.session_id = session_id;
  request.turn.user_input = user_input;
  request.turn.agent_response = agent_response;
  request.turn.created_at = 1717000000000LL +
                            static_cast<std::int64_t>(confidence_score);

  dasall::memory::FactCandidate fact_candidate;
  fact_candidate.fact.fact_text = fact_text;
  fact_candidate.fact.fact_type = "status";
  fact_candidate.fact.confidence_score = confidence_score;
  fact_candidate.fact.source_turn_ids = std::vector<std::string>{turn_id};
  fact_candidate.extraction_source = "turn";
  request.fact_candidates.push_back(std::move(fact_candidate));
  return request;
}

void test_memory_manager_prepare_context_uses_llm_backed_summarizer() {
  using dasall::tests::support::assert_true;

  const auto database_path = make_temp_database_path();
  cleanup_database_artifacts(database_path);

  auto llm_manager = std::make_shared<MockLLMManager>();
  llm_manager->set_generate_handler(
      [](const dasall::llm::LLMGenerateRequest& request) {
        return MockLLMManager::make_structured_stage_result(
            request.stage,
            R"({"schema_version":"memory_summary.v1","request_id":"req-memory-llm-summarizer","summary_text":"LLM 注入摘要：memory 生产路径已接线","decisions_made":["先补 summarizer factory，再调整 runtime 顺序"],"confirmed_facts":["CompressionCoordinator 已走 LLM 路径"],"tool_outcomes":["memory_integration:passed"]})",
            request.request.request_id);
      });

  const auto config = make_sqlite_config(database_path);
  auto manager = dasall::memory::create_memory_manager(
      config,
      dasall::memory::MemoryRuntimeDependencies{
          .summarizer_factory =
              [llm_manager](const dasall::memory::MemoryConfig&) {
                return std::make_unique<
                    dasall::apps::runtime_support::LLMBackedSummarizer>(
                    llm_manager);
              },
          .profile_id = "desktop_full",
      });

  const auto init_code = manager->init(config);
  assert_true(static_cast<int>(init_code) == 0,
              "memory compression LLM summarizer integration should initialize the sqlite-backed manager");

  const auto first_writeback = manager->write_back(make_request(
      "session-memory-llm-summarizer",
      "turn-memory-llm-summarizer-001",
      "请记录 runtime_support 的 summarizer 装配。",
      "已经补上 memory seam，下一步调整 live composition。",
      "runtime_support summarizer seam",
      88));
  assert_true(!first_writeback.result_code.has_value(),
              "memory compression LLM summarizer integration requires the first writeback to succeed");

  const auto second_writeback = manager->write_back(make_request(
      "session-memory-llm-summarizer",
      "turn-memory-llm-summarizer-002",
      "请继续记录 production 路径验证。",
      "已经确认 compression 应该命中 LLM-backed summarizer。",
      "llm-backed summarizer production path",
      93));
  assert_true(!second_writeback.result_code.has_value(),
              "memory compression LLM summarizer integration requires the second writeback to succeed");

  const auto context_result = manager->prepare_context(
      dasall::memory::MemoryContextRequest{
          .request_id = "req-memory-llm-summarizer-context",
          .session_id = "session-memory-llm-summarizer",
          .stage = "reasoning",
          .goal_summary = "验证 Memory LLM summary 注入",
          .constraints_summary = "必须优先走 LLM summarizer，而不是模板 fallback",
          .latest_observation_digest_summary = "本轮只验证 memory summarizer 生产注入路径",
          .visible_tools = {"cmake", "ctest"},
          .token_budget_hint = 256,
          .latency_budget_ms = 200,
          .external_evidence = {"build-ci integration proof"},
          .retrieval_evidence_refs = {},
      });

  assert_true(!context_result.result_code.has_value() && !context_result.degraded,
              "memory compression LLM summarizer integration should keep prepare_context successful on the LLM path");
  assert_true(context_result.context_packet.summary_memory.has_value() &&
                  context_result.context_packet.summary_memory->find(
                      "LLM 注入摘要：memory 生产路径已接线") != std::string::npos,
              "memory compression LLM summarizer integration should project the LLM summary into ContextPacket.summary_memory");
  assert_true(contains_value(context_result.compression_notes, "strategy:summarizer") &&
                  contains_value(context_result.compression_notes, "llm_summary_used"),
              "memory compression LLM summarizer integration should record the summarizer path in compression notes");
  assert_true(llm_manager->generate_requests().size() == 1U,
              "memory compression LLM summarizer integration should invoke the LLM once during compression");
  assert_true(llm_manager->generate_requests().front().stage == "response" &&
                  llm_manager->generate_requests().front().task_type == "summary",
              "memory compression LLM summarizer integration should use the response/summary selector pair");

  manager->shutdown();
  cleanup_database_artifacts(database_path);
}

}  // namespace

int main() {
  try {
    test_memory_manager_prepare_context_uses_llm_backed_summarizer();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}