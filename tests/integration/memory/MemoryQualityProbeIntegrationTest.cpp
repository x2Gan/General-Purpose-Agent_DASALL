#include <algorithm>
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
#define DASALL_SQL_MEMORY_DIR ""
#endif

#include "IMemoryManager.h"
#include "LLMBackedSummarizer.h"
#include "MockLLMManager.h"
#include "metrics/MetricsFacade.h"
#include "observability/MemoryQualityMetrics.h"
#include "support/TestAssertions.h"

namespace {

namespace fs = std::filesystem;

using dasall::apps::runtime_support::LLMBackedSummarizer;
using dasall::memory::observability::quality::kCompressionFallbackRateMetricName;
using dasall::memory::observability::quality::kFactConflictPrecisionMetricName;
using dasall::memory::observability::quality::kRecallAtKMetricName;
using dasall::memory::observability::quality::kSummaryFaithfulnessMetricName;
using dasall::memory::observability::quality::kWritebackPartialRateMetricName;
using dasall::tests::mocks::MockLLMManager;
using dasall::tests::support::assert_true;

struct QualityGroundTruthCase {
  std::string request_id;
  std::vector<std::string> external_evidence;
  std::string latest_observation_digest;
  std::optional<std::string> llm_payload;
  bool expect_fallback = false;
};

[[nodiscard]] fs::path make_temp_database_path() {
  const auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
  return fs::temp_directory_path() /
         ("dasall-memory-quality-integration-" + std::to_string(timestamp) + ".db");
}

void cleanup_database_artifacts(const fs::path& database_path) {
  (void)fs::remove(database_path);
  (void)fs::remove(database_path.string() + "-wal");
  (void)fs::remove(database_path.string() + "-shm");
}

[[nodiscard]] dasall::memory::MemoryConfig make_sqlite_config(
    const fs::path& database_path) {
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

[[nodiscard]] dasall::infra::metrics::MetricsProviderConfig make_provider_config() {
  return dasall::infra::metrics::MetricsProviderConfig{
      .enabled = true,
      .provider_type = std::string("internal"),
      .exporter_type = std::string("prom_text"),
      .reader_interval_ms = 1000,
      .exporter_timeout_ms = 10000,
  };
}

[[nodiscard]] std::shared_ptr<dasall::infra::metrics::MetricsFacade> make_metrics_facade() {
  auto facade = std::make_shared<dasall::infra::metrics::MetricsFacade>();
  assert_true(facade->init(make_provider_config()).ok,
              "memory quality probe integration should initialize MetricsFacade");
  return facade;
}

[[nodiscard]] std::string describe_metric(
    const dasall::infra::metrics::AggregatedMetricValue* metric) {
  if (metric == nullptr) {
    return "metric=null";
  }

  return std::string{"sample_count="} + std::to_string(metric->sample_count) +
         ", last=" + std::to_string(metric->last_value) +
         ", min=" + std::to_string(metric->min_value) +
         ", max=" + std::to_string(metric->max_value);
}

[[nodiscard]] dasall::memory::MemoryWritebackRequest make_request(
    const std::string& session_id,
    const std::string& turn_id,
    const std::string& user_input,
    const std::string& agent_response,
    const std::string& fact_text,
    std::uint32_t confidence_score,
    bool inject_invalid_fact) {
  dasall::memory::MemoryWritebackRequest request;
  request.session_id = session_id;
  request.turn.turn_id = turn_id;
  request.turn.session_id = session_id;
  request.turn.user_input = user_input;
  request.turn.agent_response = agent_response;
  request.turn.created_at = 7000 + static_cast<std::int64_t>(confidence_score);

  dasall::memory::FactCandidate fact_candidate;
  fact_candidate.fact.fact_text = fact_text;
  fact_candidate.fact.fact_type = "status";
  fact_candidate.fact.confidence_score = confidence_score;
  fact_candidate.fact.source_turn_ids = std::vector<std::string>{turn_id};
  fact_candidate.extraction_source = "turn";
  request.fact_candidates.push_back(std::move(fact_candidate));

  if (inject_invalid_fact) {
    dasall::memory::FactCandidate invalid_candidate;
    invalid_candidate.fact.fact_type = "status";
    invalid_candidate.fact.confidence_score = confidence_score;
    invalid_candidate.extraction_source = "turn";
    request.fact_candidates.push_back(std::move(invalid_candidate));
  }

  return request;
}

void test_quality_probe_integration_emits_quality_metric_baselines() {
  const auto database_path = make_temp_database_path();
  cleanup_database_artifacts(database_path);

  auto metrics_facade = make_metrics_facade();
  auto llm_manager = std::make_shared<MockLLMManager>();
  const auto config = make_sqlite_config(database_path);
  dasall::memory::MemoryRuntimeDependencies runtime_dependencies;
  runtime_dependencies.metrics_provider = metrics_facade;
  runtime_dependencies.summarizer_factory =
      [llm_manager](const dasall::memory::MemoryConfig&) {
        return std::make_unique<LLMBackedSummarizer>(llm_manager);
      };
  runtime_dependencies.profile_id = "desktop_full";
  auto manager = dasall::memory::create_memory_manager(
      config,
      runtime_dependencies);

  const auto init_code = manager->init(config);
  assert_true(static_cast<int>(init_code) == 0,
              "memory quality probe integration should initialize the sqlite-backed manager");

  const auto first_writeback = manager->write_back(make_request(
      "session-memory-quality",
      "turn-memory-quality-001",
      "network mode enabled",
      "retry budget 3",
      "network mode enabled",
      70,
      false));
  assert_true(!first_writeback.result_code.has_value() && !first_writeback.partial,
              "memory quality probe integration requires the first writeback to succeed without partial degradation");

  const auto conflict_writeback = manager->write_back(make_request(
      "session-memory-quality",
      "turn-memory-quality-002",
      "update network mode",
      "network mode disabled",
      "network mode disabled",
      95,
      false));
  assert_true(!conflict_writeback.result_code.has_value() &&
                  conflict_writeback.conflicts.size() == 1U &&
                  conflict_writeback.conflicts.front().action ==
                      dasall::memory::ConflictAction::Supersede,
              "memory quality probe integration requires one successful supersede conflict to seed conflict precision");

  const auto partial_writeback = manager->write_back(make_request(
      "session-memory-quality",
      "turn-memory-quality-003",
      "stabilize retry budget",
      "retry budget 3 remains active",
      "retry budget 3",
      88,
      true));
  assert_true(!partial_writeback.result_code.has_value() && partial_writeback.partial,
              "memory quality probe integration requires a partial writeback to seed the partial-rate metric");

  const std::vector<QualityGroundTruthCase> ground_truth_cases{
      QualityGroundTruthCase{
          .request_id = "req-memory-quality-summarizer",
          .external_evidence = {
              "network mode disabled",
              "retry budget 3",
          },
          .latest_observation_digest = "retry budget 3",
          .llm_payload = std::string(
              R"({"schema_version":"memory_summary.v1","summary_text":"network mode disabled. retry budget 3.","decisions_made":[],"confirmed_facts":["network mode disabled"],"tool_outcomes":[]})"),
          .expect_fallback = false,
      },
      QualityGroundTruthCase{
          .request_id = "req-memory-quality-fallback",
          .external_evidence = {
              "network mode disabled",
              "retry budget 3",
          },
          .latest_observation_digest = "retry budget 3",
          .llm_payload = std::nullopt,
          .expect_fallback = true,
      },
  };

  for (const auto& ground_truth : ground_truth_cases) {
    if (ground_truth.llm_payload.has_value()) {
      llm_manager->set_generate_result(MockLLMManager::make_structured_stage_result(
          "response",
          *ground_truth.llm_payload,
          ground_truth.request_id));
    } else {
      llm_manager->set_generate_result(MockLLMManager::make_failure_result(
          dasall::contracts::ResultCode::RuntimeRetryExhausted,
          "forced summarizer fallback",
          dasall::llm::LLMFailureCategory::ProviderProtocol,
          "mock.summary.route",
          ground_truth.request_id));
    }

      dasall::memory::MemoryContextRequest context_request;
      context_request.request_id = ground_truth.request_id;
      context_request.session_id = "session-memory-quality";
      context_request.stage = "reasoning";
      context_request.goal_summary =
        "measure end-to-end memory quality metrics";
      context_request.latest_observation_digest_summary =
        ground_truth.latest_observation_digest;
      context_request.token_budget_hint = 224;
      context_request.external_evidence = ground_truth.external_evidence;

      const auto context_result = manager->prepare_context(context_request);
    assert_true(!context_result.result_code.has_value() &&
                    context_result.context_packet.summary_memory.has_value() &&
                    !context_result.compression_notes.empty(),
                "memory quality probe integration should produce a compressed context packet for every curated ground-truth case");

    const bool observed_fallback =
        std::find(context_result.compression_notes.begin(),
                  context_result.compression_notes.end(),
                  "summarizer_fallback") != context_result.compression_notes.end();
    assert_true(observed_fallback == ground_truth.expect_fallback,
                "memory quality probe integration should preserve the expected compression fallback behavior for each curated case");
  }

  const auto snapshot = metrics_facade->aggregation_snapshot();
  const auto* recall_metric = snapshot.find(std::string(kRecallAtKMetricName));
  const auto* faithfulness_metric =
      snapshot.find(std::string(kSummaryFaithfulnessMetricName));
  const auto* conflict_precision_metric =
      snapshot.find(std::string(kFactConflictPrecisionMetricName));
  const auto* partial_rate_metric =
      snapshot.find(std::string(kWritebackPartialRateMetricName));
  const auto* compression_fallback_metric =
      snapshot.find(std::string(kCompressionFallbackRateMetricName));

  assert_true(recall_metric != nullptr && recall_metric->sample_count == 2U &&
                  recall_metric->last_value > 0.99,
              "memory quality probe integration should emit recall@k for both curated context cases");
  assert_true(faithfulness_metric != nullptr &&
                  faithfulness_metric->sample_count == 2U &&
                  faithfulness_metric->last_value > 0.0,
              "memory quality probe integration should emit summary faithfulness for both curated context cases");
  assert_true(conflict_precision_metric != nullptr &&
            conflict_precision_metric->sample_count >= 1U &&
            conflict_precision_metric->last_value > 0.99 &&
            conflict_precision_metric->min_value > 0.99,
          "memory quality probe integration should emit a perfect conflict precision baseline for the successful supersede case: " +
            describe_metric(conflict_precision_metric));
  assert_true(partial_rate_metric != nullptr &&
                  partial_rate_metric->sample_count == 3U &&
                  partial_rate_metric->last_value > 0.0 &&
                  partial_rate_metric->last_value < 1.0,
              "memory quality probe integration should emit a non-zero but bounded writeback partial rate after one partial writeback in three attempts");
  assert_true(compression_fallback_metric != nullptr &&
                  compression_fallback_metric->sample_count == 2U &&
                  compression_fallback_metric->min_value == 0.0 &&
                  compression_fallback_metric->max_value >= 0.5 &&
                  compression_fallback_metric->last_value >= 0.5,
              "memory quality probe integration should emit a rolling compression fallback rate after one summarizer success and one forced fallback");

  manager->shutdown();
  cleanup_database_artifacts(database_path);
}

}  // namespace

int main() {
  try {
    test_quality_probe_integration_emits_quality_metric_baselines();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}
