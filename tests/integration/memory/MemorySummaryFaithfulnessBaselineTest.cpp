#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef DASALL_SQL_MEMORY_DIR
#define DASALL_SQL_MEMORY_DIR ""
#endif

#include "IMemoryManager.h"
#include "metrics/MetricsFacade.h"
#include "observability/MemoryQualityMetrics.h"
#include "support/TestAssertions.h"

namespace {

namespace fs = std::filesystem;

using dasall::memory::observability::quality::kSummaryFaithfulnessMetricName;
using dasall::tests::support::assert_true;

[[nodiscard]] fs::path make_temp_database_path() {
  const auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
  return fs::temp_directory_path() /
         ("dasall-memory-faithfulness-baseline-" + std::to_string(timestamp) + ".db");
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
  config.context.compression_trigger_turns = 8;
  config.context.compression_trigger_ratio = 0.90;
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
              "memory summary faithfulness baseline test should initialize MetricsFacade");
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
    const std::string& summary_text,
    std::uint32_t confidence) {
  dasall::memory::MemoryWritebackRequest request;
  request.session_id = session_id;
  request.turn.turn_id = turn_id;
  request.turn.session_id = session_id;
  request.turn.user_input = user_input;
  request.turn.agent_response = agent_response;
  request.turn.created_at = 4000 + static_cast<std::int64_t>(confidence);
  request.summary_candidate = dasall::contracts::SummaryMemory{};
  request.summary_candidate->summary_text = summary_text;
  request.summary_candidate->confirmed_facts = std::vector<std::string>{fact_text};

  dasall::memory::FactCandidate fact_candidate;
  fact_candidate.fact.fact_text = fact_text;
  fact_candidate.fact.fact_type = "status";
  fact_candidate.fact.confidence_score = confidence;
  fact_candidate.fact.source_turn_ids = std::vector<std::string>{turn_id};
  fact_candidate.extraction_source = "turn";
  request.fact_candidates.push_back(std::move(fact_candidate));
  return request;
}

void test_summary_faithfulness_baseline_tracks_supported_and_unsupported_claims() {
  const auto database_path = make_temp_database_path();
  cleanup_database_artifacts(database_path);

  auto metrics_facade = make_metrics_facade();
  const auto config = make_sqlite_config(database_path);
    dasall::memory::MemoryRuntimeDependencies runtime_dependencies;
    runtime_dependencies.metrics_provider = metrics_facade;
    runtime_dependencies.profile_id = "desktop_full";
  auto manager = dasall::memory::create_memory_manager(
      config,
      runtime_dependencies);

  const auto init_code = manager->init(config);
  assert_true(static_cast<int>(init_code) == 0,
              "memory summary faithfulness baseline test should initialize the sqlite-backed manager");

  const auto faithful_writeback = manager->write_back(make_request(
      "session-memory-faithfulness-faithful",
      "turn-memory-faithfulness-001",
      "network mode enabled",
      "retry budget 3",
      "network mode enabled",
      "network mode enabled.",
      91));
  assert_true(!faithful_writeback.result_code.has_value(),
              "memory summary faithfulness baseline test should persist the faithful summary fixture");

    dasall::memory::MemoryContextRequest faithful_context_request;
    faithful_context_request.request_id =
      "req-memory-faithfulness-faithful";
    faithful_context_request.session_id =
      "session-memory-faithfulness-faithful";
    faithful_context_request.stage = "reasoning";
    faithful_context_request.goal_summary = "measure faithful summary grounding";
    faithful_context_request.latest_observation_digest_summary =
      "retry budget 3";
    faithful_context_request.token_budget_hint = 192;

    const auto faithful_context =
      manager->prepare_context(faithful_context_request);
  assert_true(!faithful_context.result_code.has_value() &&
                  faithful_context.context_packet.summary_memory.has_value(),
              "memory summary faithfulness baseline test should expose the faithful summary in ContextPacket");

  const auto unfaithful_writeback = manager->write_back(make_request(
      "session-memory-faithfulness-unfaithful",
      "turn-memory-faithfulness-002",
      "network mode disabled",
      "retry budget 3",
      "network mode disabled",
      "network mode disabled. maintenance window tomorrow.",
      93));
  assert_true(!unfaithful_writeback.result_code.has_value(),
              "memory summary faithfulness baseline test should persist the partially unsupported summary fixture");

    dasall::memory::MemoryContextRequest unfaithful_context_request;
    unfaithful_context_request.request_id =
      "req-memory-faithfulness-unfaithful";
    unfaithful_context_request.session_id =
      "session-memory-faithfulness-unfaithful";
    unfaithful_context_request.stage = "reasoning";
    unfaithful_context_request.goal_summary =
      "measure partially unsupported summary grounding";
    unfaithful_context_request.latest_observation_digest_summary =
      "retry budget 3";
    unfaithful_context_request.token_budget_hint = 192;

    const auto unfaithful_context =
      manager->prepare_context(unfaithful_context_request);
  assert_true(!unfaithful_context.result_code.has_value() &&
                  unfaithful_context.context_packet.summary_memory.has_value(),
              "memory summary faithfulness baseline test should expose the partially unsupported summary in ContextPacket");

  const auto snapshot = metrics_facade->aggregation_snapshot();
  const auto* faithfulness_metric =
      snapshot.find(std::string(kSummaryFaithfulnessMetricName));
  assert_true(faithfulness_metric != nullptr &&
                  faithfulness_metric->identity.type ==
                      dasall::infra::metrics::MetricType::Gauge &&
                  faithfulness_metric->sample_count == 2U &&
                  faithfulness_metric->max_value > 0.99 &&
                  faithfulness_metric->min_value < 1.0 &&
                  faithfulness_metric->last_value < 1.0,
          "memory summary faithfulness baseline test should emit a gauge metric whose baseline spans fully grounded and partially unsupported summaries: " +
            describe_metric(faithfulness_metric));

  manager->shutdown();
  cleanup_database_artifacts(database_path);
}

}  // namespace

int main() {
  try {
    test_summary_faithfulness_baseline_tracks_supported_and_unsupported_claims();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}
