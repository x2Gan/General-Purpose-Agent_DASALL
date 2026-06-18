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

using dasall::memory::observability::quality::kRecallAtKMetricName;
using dasall::tests::support::assert_true;

[[nodiscard]] fs::path make_temp_database_path() {
  const auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
  return fs::temp_directory_path() /
         ("dasall-memory-recall-baseline-" + std::to_string(timestamp) + ".db");
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
              "memory recall baseline test should initialize MetricsFacade");
  return facade;
}

void test_recall_at_k_baseline_captures_full_and_trimmed_projection() {
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
              "memory recall baseline test should initialize the sqlite-backed manager");

  const std::vector<std::string> evidence = {
      "alpha quality evidence anchor retained in retrieval evidence with extra budget pressure tokens",
      "beta quality evidence anchor retained in retrieval evidence with extra budget pressure tokens",
      "gamma quality evidence anchor retained in retrieval evidence with extra budget pressure tokens",
  };

    dasall::memory::MemoryContextRequest full_budget_request;
    full_budget_request.request_id = "req-memory-recall-full";
    full_budget_request.session_id = "session-memory-recall";
    full_budget_request.stage = "reasoning";
    full_budget_request.goal_summary =
      "measure memory recall quality under full budget";
    full_budget_request.constraints_summary =
      "keep every curated evidence entry visible";
    full_budget_request.token_budget_hint = 512;
    full_budget_request.external_evidence = evidence;

    const auto full_budget_result = manager->prepare_context(full_budget_request);
  assert_true(!full_budget_result.result_code.has_value() &&
                  full_budget_result.context_packet.retrieval_evidence.has_value() &&
                  full_budget_result.context_packet.retrieval_evidence->size() >= evidence.size(),
              "memory recall baseline test should keep all curated evidence under the relaxed budget");

    dasall::memory::MemoryContextRequest tight_budget_request;
    tight_budget_request.request_id = "req-memory-recall-tight";
    tight_budget_request.session_id = "session-memory-recall";
    tight_budget_request.stage = "reasoning";
    tight_budget_request.goal_summary =
      "measure memory recall quality under tight budget";
    tight_budget_request.constraints_summary =
      "only the highest-priority evidence should survive a tiny budget";
    tight_budget_request.latest_observation_digest_summary =
      "budget is intentionally tiny for this recall stress case";
    tight_budget_request.token_budget_hint = 8;
    tight_budget_request.external_evidence = evidence;

    const auto tight_budget_result = manager->prepare_context(tight_budget_request);
  assert_true(!tight_budget_result.result_code.has_value() &&
                  tight_budget_result.context_packet.retrieval_evidence.has_value() &&
                  tight_budget_result.context_packet.retrieval_evidence->size() < evidence.size(),
              "memory recall baseline test should trim at least one curated evidence item when the token budget is tight");

  const auto snapshot = metrics_facade->aggregation_snapshot();
  const auto* recall_metric = snapshot.find(std::string(kRecallAtKMetricName));
  assert_true(recall_metric != nullptr &&
                  recall_metric->identity.type ==
                      dasall::infra::metrics::MetricType::Gauge &&
                  recall_metric->sample_count == 2U &&
                  recall_metric->max_value > 0.99 &&
                  recall_metric->min_value < 1.0 &&
                  recall_metric->last_value < 1.0,
              "memory recall baseline test should emit a gauge metric whose baseline spans both full recall and trimmed recall cases");

  manager->shutdown();
  cleanup_database_artifacts(database_path);
}

}  // namespace

int main() {
  try {
    test_recall_at_k_baseline_captures_full_and_trimmed_projection();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}
