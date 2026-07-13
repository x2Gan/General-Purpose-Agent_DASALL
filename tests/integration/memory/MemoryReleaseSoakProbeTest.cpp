#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <sqlite3.h>

#ifndef DASALL_SQL_MEMORY_DIR
#define DASALL_SQL_MEMORY_DIR ""
#endif

#include "IMemoryManager.h"
#include "ILLMTransport.h"
#include "LLMBackedEmbeddingAdapter.h"
#include "LLMBackedSummarizer.h"
#include "MockLLMManager.h"
#include "metrics/MetricsFacade.h"
#include "observability/MemoryQualityMetrics.h"
#include "support/TestAssertions.h"
#include "store/sqlite/SqliteMemoryStore.h"
#include "vector/SimpleLocalEmbeddingAdapter.h"
#include "vector/SqliteVssVectorBackend.h"

namespace {

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

using dasall::apps::runtime_support::LLMBackedSummarizer;
using dasall::memory::observability::quality::kCompressionFallbackRateMetricName;
using dasall::memory::observability::quality::kRecallAtKMetricName;
using dasall::memory::observability::quality::kWritebackPartialRateMetricName;
using dasall::tests::mocks::MockLLMManager;
using dasall::tests::support::assert_true;

constexpr int kSoakIterationCount = 320;
constexpr int kSoakBatchSize = 8;
constexpr int kSummaryInterval = 64;
constexpr std::uintmax_t kMaxWalBytes = 2U * 1024U * 1024U;
constexpr int kVectorRecallK = 1;
constexpr char kSummaryFileName[] = "memory-release-soak-summary.json";

struct LatencyStats {
  std::size_t sample_count = 0U;
  double min_ms = 0.0;
  double avg_ms = 0.0;
  double p95_ms = 0.0;
  double max_ms = 0.0;
  double last_ms = 0.0;
};

struct RollingRateStats {
  std::size_t sample_count = 0U;
  double min_value = 0.0;
  double max_value = 0.0;
  double last_value = 0.0;
};

struct LongRunningSoakSample {
  LatencyStats store_latency_ms;
  LatencyStats maintenance_lag_ms;
  std::uintmax_t max_wal_bytes = 0U;
  int turns_purged = 0;
  int facts_purged = 0;
  int experiences_purged = 0;
  int quarantine_cleaned = 0;
};

struct QualityMetricSample {
  RollingRateStats writeback_partial_rate;
  RollingRateStats summary_fallback_rate;
  RollingRateStats retrieval_recall_at_k;
};

struct VectorRecallSample {
  int k = kVectorRecallK;
  double local_baseline = 0.0;
  double semantic_adapter = 0.0;
  int provider_calls = 0;
};

struct ReleaseSoakSummary {
  LongRunningSoakSample long_running;
  QualityMetricSample quality;
  VectorRecallSample vector_recall;
};

struct RecallFixture {
  std::string query_text;
  std::string relevant_doc_text;
  std::string distractor_doc_text;
};

[[nodiscard]] double elapsed_ms(const Clock::time_point start,
                                const Clock::time_point end) {
  return std::chrono::duration<double, std::milli>(end - start).count();
}

[[nodiscard]] std::int64_t current_time_millis() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

[[nodiscard]] fs::path make_temp_database_path(const std::string& stem) {
  const auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
  return fs::temp_directory_path() /
         (stem + "-" + std::to_string(timestamp) + ".db");
}

void cleanup_database_artifacts(const fs::path& database_path) {
  (void)fs::remove(database_path);
  (void)fs::remove(database_path.string() + "-wal");
  (void)fs::remove(database_path.string() + "-shm");
}

[[nodiscard]] LatencyStats compute_latency_stats(
    const std::vector<double>& samples) {
  assert_true(!samples.empty(),
              "memory release soak probe requires at least one latency sample");
  std::vector<double> sorted_samples = samples;
  std::sort(sorted_samples.begin(), sorted_samples.end());

  double total = 0.0;
  for (const double value : samples) {
    total += value;
  }

  const std::size_t percentile_index =
      std::min<std::size_t>(sorted_samples.size() - 1U,
                            static_cast<std::size_t>(
                                (sorted_samples.size() - 1U) * 0.95));
  return LatencyStats{
      .sample_count = samples.size(),
      .min_ms = sorted_samples.front(),
      .avg_ms = total / static_cast<double>(samples.size()),
      .p95_ms = sorted_samples[percentile_index],
      .max_ms = sorted_samples.back(),
      .last_ms = samples.back(),
  };
}

void write_latency_stats_json(std::ostream& stream,
                              const LatencyStats& stats,
                              const int indent) {
  const std::string prefix(static_cast<std::size_t>(indent), ' ');
  stream << "{\n"
         << prefix << "  \"sample_count\": " << stats.sample_count << ",\n"
         << prefix << "  \"min_ms\": " << std::fixed << std::setprecision(3)
         << stats.min_ms << ",\n"
         << prefix << "  \"avg_ms\": " << stats.avg_ms << ",\n"
         << prefix << "  \"p95_ms\": " << stats.p95_ms << ",\n"
         << prefix << "  \"max_ms\": " << stats.max_ms << ",\n"
         << prefix << "  \"last_ms\": " << stats.last_ms << "\n"
         << prefix << "}";
}

void write_rate_stats_json(std::ostream& stream,
                           const RollingRateStats& stats,
                           const int indent) {
  const std::string prefix(static_cast<std::size_t>(indent), ' ');
  stream << "{\n"
         << prefix << "  \"sample_count\": " << stats.sample_count << ",\n"
         << prefix << "  \"min_value\": " << std::fixed << std::setprecision(6)
         << stats.min_value << ",\n"
         << prefix << "  \"max_value\": " << stats.max_value << ",\n"
         << prefix << "  \"last_value\": " << stats.last_value << "\n"
         << prefix << "}";
}

void write_summary_json(const ReleaseSoakSummary& summary,
                        const fs::path& output_path) {
  fs::create_directories(output_path.parent_path());
  std::ofstream stream(output_path, std::ios::binary | std::ios::trunc);
  assert_true(stream.is_open(),
              "memory release soak probe should write the summary artifact");

  stream << "{\n"
         << "  \"schema_version\": 1,\n"
         << "  \"authoritative_test_binary\": \"MemoryReleaseSoakProbeTest\",\n"
         << "  \"release_runner_local_artifact_ready\": true,\n"
         << "  \"iterations_requested\": " << kSoakIterationCount << ",\n"
         << "  \"iterations_completed\": " << kSoakIterationCount << ",\n"
         << "  \"store_latency_ms\": ";
  write_latency_stats_json(stream, summary.long_running.store_latency_ms, 2);
  stream << ",\n"
         << "  \"wal_size_bytes\": {\n"
         << "    \"max\": " << summary.long_running.max_wal_bytes << ",\n"
         << "    \"ceiling\": " << kMaxWalBytes << "\n"
         << "  },\n"
         << "  \"maintenance_lag_ms\": ";
  write_latency_stats_json(stream, summary.long_running.maintenance_lag_ms, 2);
  stream << ",\n"
         << "  \"writeback_partial_rate\": ";
  write_rate_stats_json(stream, summary.quality.writeback_partial_rate, 2);
  stream << ",\n"
         << "  \"summary_fallback_rate\": ";
  write_rate_stats_json(stream, summary.quality.summary_fallback_rate, 2);
  stream << ",\n"
         << "  \"vector_recall_at_k\": {\n"
         << "    \"k\": " << summary.vector_recall.k << ",\n"
         << "    \"local_baseline\": " << std::fixed << std::setprecision(6)
         << summary.vector_recall.local_baseline << ",\n"
         << "    \"semantic_adapter\": " << summary.vector_recall.semantic_adapter << ",\n"
         << "    \"improvement\": "
         << (summary.vector_recall.semantic_adapter -
             summary.vector_recall.local_baseline)
         << ",\n"
         << "    \"provider_calls\": " << summary.vector_recall.provider_calls << "\n"
         << "  },\n"
         << "  \"retrieval_recall_at_k\": ";
  write_rate_stats_json(stream, summary.quality.retrieval_recall_at_k, 2);
  stream << ",\n"
         << "  \"maintenance_cleanup_totals\": {\n"
         << "    \"turns_purged\": " << summary.long_running.turns_purged << ",\n"
         << "    \"facts_purged\": " << summary.long_running.facts_purged << ",\n"
         << "    \"experiences_purged\": " << summary.long_running.experiences_purged << ",\n"
         << "    \"quarantine_cleaned\": "
         << summary.long_running.quarantine_cleaned << "\n"
         << "  },\n"
         << "  \"covered_slices\": [\n"
         << "    \"store_latency\",\n"
         << "    \"wal_size\",\n"
         << "    \"maintenance_lag\",\n"
         << "    \"writeback_partial_rate\",\n"
         << "    \"vector_recall_at_k\",\n"
         << "    \"summary_fallback_rate\"\n"
         << "  ]\n"
         << "}\n";
}

void execute_sql(const fs::path& database_path,
                 const std::string& sql) {
  sqlite3* connection = nullptr;
  if (sqlite3_open(database_path.string().c_str(), &connection) != SQLITE_OK) {
    throw std::runtime_error(
        "failed to open sqlite connection for memory release soak probe");
  }

  char* error_message = nullptr;
  const int sqlite_status =
      sqlite3_exec(connection, sql.c_str(), nullptr, nullptr, &error_message);
  if (sqlite_status != SQLITE_OK) {
    const std::string message =
        error_message == nullptr ? "failed to execute sqlite statement"
                                 : error_message;
    sqlite3_free(error_message);
    sqlite3_close(connection);
    throw std::runtime_error(message);
  }

  sqlite3_close(connection);
}

int query_scalar_count(const fs::path& database_path,
                       const std::string& sql) {
  sqlite3* connection = nullptr;
  if (sqlite3_open(database_path.string().c_str(), &connection) != SQLITE_OK) {
    throw std::runtime_error(
        "failed to open sqlite connection for count query");
  }

  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(connection, sql.c_str(), -1, &statement, nullptr) !=
      SQLITE_OK) {
    sqlite3_close(connection);
    throw std::runtime_error("failed to prepare sqlite count query");
  }

  int value = 0;
  if (sqlite3_step(statement) == SQLITE_ROW) {
    value = sqlite3_column_int(statement, 0);
  }

  sqlite3_finalize(statement);
  sqlite3_close(connection);
  return value;
}

[[nodiscard]] bool has_warning(const std::vector<std::string>& warnings,
                               const std::string& warning_key) {
  return std::find(warnings.begin(), warnings.end(), warning_key) !=
         warnings.end();
}

[[nodiscard]] std::uintmax_t wal_size_bytes(const fs::path& database_path) {
  const auto wal_path = fs::path(database_path.string() + "-wal");
  if (!fs::exists(wal_path)) {
    return 0U;
  }
  return fs::file_size(wal_path);
}

[[nodiscard]] dasall::memory::MemoryConfig make_soak_sqlite_config(
    const fs::path& database_path) {
  dasall::memory::MemoryConfig config;
  config.storage.backend = dasall::memory::StorageBackend::Sqlite;
  config.storage.db_path = database_path.string();
  config.storage.reader_pool_size = 2;
  config.storage.migrations_dir = DASALL_SQL_MEMORY_DIR;
  config.storage.wal_autocheckpoint_pages = 8;
  config.storage.busy_timeout_ms = 100;
  config.context.compression_trigger_turns = 6;
  config.context.compression_trigger_ratio = 0.5;
  config.compression.hierarchy.enabled = true;
  config.compression.hierarchy.dialog_to_topic_threshold = 2;
  config.compression.hierarchy.topic_to_profile_threshold = 2;
  config.maintenance.retention_turns = 24;
  config.maintenance.fact_ttl_ms = 5000;
  config.maintenance.experience_ttl_ms = 5000;
  config.maintenance.quarantine_ttl_ms = 5000;
  config.vector.enabled = false;
  return config;
}

[[nodiscard]] dasall::memory::MemoryWritebackRequest make_soak_request(
    const int iteration) {
  const auto simulated_time = current_time_millis() - 300000 + (iteration * 1000);
  const std::string session_id = "session-memory-release-soak";
  const std::string turn_id =
      "turn-memory-release-soak-" + std::to_string(iteration);

  dasall::memory::MemoryWritebackRequest request;
  request.request_id =
      "writeback-memory-release-soak-" + std::to_string(iteration);
  request.session_id = session_id;
  request.trace_id = "trace-memory-release-soak";
  request.turn.turn_id = turn_id;
  request.turn.session_id = session_id;
  request.turn.user_input = "soak iteration " + std::to_string(iteration);
  request.turn.agent_response =
      (iteration % 2) == 0 ? "soak mode enabled" : "soak mode disabled";
  request.turn.created_at = simulated_time;

  if ((iteration % kSummaryInterval) == 0) {
    request.summary_candidate = dasall::contracts::SummaryMemory{};
    request.summary_candidate->summary_text = "summary for " + turn_id;
    request.summary_candidate->source_turn_ids = std::vector<std::string>{turn_id};
    request.summary_candidate->confirmed_facts =
        std::vector<std::string>{"soak cadence checkpoint"};
    request.summary_candidate->created_at = simulated_time;
  }

  dasall::memory::FactCandidate fact_candidate;
  fact_candidate.fact.fact_text =
      (iteration % 2) == 0 ? "soak mode enabled" : "soak mode disabled";
  fact_candidate.fact.fact_type = "soak_state";
  fact_candidate.fact.confidence_score = 90;
  fact_candidate.fact.source_turn_ids = std::vector<std::string>{turn_id};
  fact_candidate.fact.created_at = simulated_time;
  fact_candidate.extraction_source = "soak";
  request.fact_candidates.push_back(std::move(fact_candidate));

  return request;
}

void seed_stale_retention_records(
    dasall::memory::store::sqlite::SqliteMemoryStore& store,
    const fs::path& database_path,
    const int batch_index,
    const std::string& session_id,
    const std::string& source_turn_id) {
  dasall::contracts::MemoryFact stale_fact;
  stale_fact.fact_id = "stale-fact-release-soak-" + std::to_string(batch_index);
  stale_fact.session_id = session_id;
  stale_fact.fact_text = "stale soak fact";
  stale_fact.source_turn_ids = std::vector<std::string>{source_turn_id};
  stale_fact.confidence_score = 60;
  stale_fact.fact_type = "soak_state";
  stale_fact.created_at = 1;
  assert_true(store.insert_fact(stale_fact).ok,
              "memory release soak probe should seed a stale fact");
  assert_true(
      store.supersede_fact(*stale_fact.fact_id,
                           "fresh-fact-release-soak-" +
                               std::to_string(batch_index))
          .ok,
      "memory release soak probe should supersede the stale fact");

  dasall::contracts::ExperienceMemory stale_experience;
  stale_experience.experience_id =
      "stale-experience-release-soak-" + std::to_string(batch_index);
  stale_experience.session_id = session_id;
  stale_experience.lesson_summary = "drop the stale soak experience";
  stale_experience.trigger_condition = "release soak retention cleanup";
  stale_experience.recommended_action =
      "purge superseded expired experience";
  stale_experience.source_turn_ids = std::vector<std::string>{source_turn_id};
  stale_experience.created_at = 1;
  stale_experience.expires_at = 2;
  stale_experience.superseded_by_experience_id =
      "fresh-experience-release-soak-" + std::to_string(batch_index);
  assert_true(store.insert_experience(stale_experience).ok,
              "memory release soak probe should seed a stale experience");

  const std::string quarantine_id =
      "stale-quarantine-release-soak-" + std::to_string(batch_index);
  assert_true(store.quarantine_record("turn", quarantine_id,
                                      "release-soak-retention")
                  .ok,
              "memory release soak probe should seed a stale quarantine row");
  execute_sql(database_path,
              "UPDATE quarantined_records SET created_at = 1 WHERE object_id = '" +
                  quarantine_id + "'");
}

[[nodiscard]] LongRunningSoakSample run_long_running_soak_probe() {
  const auto database_path = make_temp_database_path("dasall-memory-release-soak");
  cleanup_database_artifacts(database_path);

  const auto config = make_soak_sqlite_config(database_path);
  assert_true(!config.storage.migrations_dir.empty(),
              "memory release soak probe requires DASALL_SQL_MEMORY_DIR");

  auto manager = dasall::memory::create_memory_manager(config);
  const auto init_code = manager->init(config);
  assert_true(static_cast<int>(init_code) == 0,
              "memory release soak probe requires a sqlite-backed memory manager");

  dasall::memory::store::sqlite::SqliteMemoryStore seeding_store;
  assert_true(!seeding_store.open(config).has_value(),
              "memory release soak probe requires a direct sqlite store for retention seeding");

  std::vector<double> writeback_latencies;
  std::vector<double> maintenance_latencies;
  std::uintmax_t max_wal_bytes = 0U;
  int total_turns_purged = 0;
  int total_facts_purged = 0;
  int total_experiences_purged = 0;
  int total_quarantine_cleaned = 0;
  bool checkpoint_executed = false;
  std::string latest_turn_id;

  for (int batch_start = 0; batch_start < kSoakIterationCount;
       batch_start += kSoakBatchSize) {
    const int batch_end =
        std::min(batch_start + kSoakBatchSize, kSoakIterationCount);
    for (int iteration = batch_start; iteration < batch_end; ++iteration) {
      const auto start = Clock::now();
      const auto result = manager->write_back(make_soak_request(iteration));
      const auto end = Clock::now();
      writeback_latencies.push_back(elapsed_ms(start, end));
      assert_true(!result.result_code.has_value() &&
                      result.persisted_turn_id.has_value(),
                  "memory release soak probe should keep write_back healthy across the soak window");
      latest_turn_id = *result.persisted_turn_id;
    }

    seed_stale_retention_records(seeding_store,
                                 database_path,
                                 batch_start / kSoakBatchSize,
                                 "session-memory-release-soak",
                                 latest_turn_id);

    dasall::memory::MaintenanceRequest request;
    request.request_id =
        "maintenance-memory-release-soak-" + std::to_string(batch_start);
    request.trace_id = "trace-memory-release-soak";
    request.run_checkpoint = true;
    request.run_retention = true;
    request.run_quarantine_cleanup = true;
    request.run_vector_rebuild = false;

    const auto start = Clock::now();
    const auto report = manager->run_maintenance(request);
    const auto end = Clock::now();
    maintenance_latencies.push_back(elapsed_ms(start, end));

    assert_true(!has_warning(report.warnings, "maintenance_store_not_open"),
                "memory release soak probe should keep maintenance store wired");
    assert_true(!has_warning(report.warnings, "checkpoint_starvation"),
                "memory release soak probe should avoid WAL checkpoint starvation");
    assert_true(!has_warning(report.warnings, "turn_retention_failed") &&
                    !has_warning(report.warnings,
                                 "turn_retention_commit_failed") &&
                    !has_warning(report.warnings, "fact_retention_failed") &&
                    !has_warning(report.warnings,
                                 "experience_retention_failed") &&
                    !has_warning(report.warnings,
                                 "quarantine_cleanup_failed"),
                "memory release soak probe should complete maintenance without storage warnings");

    checkpoint_executed = checkpoint_executed || report.checkpoint_executed;
    total_turns_purged += report.turns_purged;
    total_facts_purged += report.facts_purged;
    total_experiences_purged += report.experiences_purged;
    total_quarantine_cleaned += report.quarantine_cleaned;
    max_wal_bytes = std::max(max_wal_bytes, wal_size_bytes(database_path));
  }

  assert_true(checkpoint_executed,
              "memory release soak probe should execute at least one WAL checkpoint");
  assert_true(total_turns_purged > 0,
              "memory release soak probe should purge stale turns");
  assert_true(total_facts_purged >= (kSoakIterationCount / kSoakBatchSize),
              "memory release soak probe should purge every seeded stale fact");
  assert_true(total_experiences_purged >=
                  (kSoakIterationCount / kSoakBatchSize),
              "memory release soak probe should purge every seeded stale experience");
  assert_true(total_quarantine_cleaned >=
                  (kSoakIterationCount / kSoakBatchSize),
              "memory release soak probe should clean every seeded quarantine row");
  assert_true(max_wal_bytes <= kMaxWalBytes,
              "memory release soak probe should keep WAL growth bounded");

  assert_true(
      query_scalar_count(
          database_path,
          "SELECT COUNT(*) FROM facts WHERE fact_id LIKE 'stale-fact-release-soak-%'") ==
          0,
      "memory release soak probe should remove all seeded stale fact rows");
  assert_true(
      query_scalar_count(
          database_path,
          "SELECT COUNT(*) FROM experiences WHERE experience_id LIKE 'stale-experience-release-soak-%'") ==
          0,
      "memory release soak probe should remove all seeded stale experience rows");
  assert_true(
      query_scalar_count(
          database_path,
          "SELECT COUNT(*) FROM quarantined_records WHERE object_id LIKE 'stale-quarantine-release-soak-%'") ==
          0,
      "memory release soak probe should remove all seeded stale quarantine rows");

  seeding_store.close();
  manager->shutdown();
  cleanup_database_artifacts(database_path);

  return LongRunningSoakSample{
      .store_latency_ms = compute_latency_stats(writeback_latencies),
      .maintenance_lag_ms = compute_latency_stats(maintenance_latencies),
      .max_wal_bytes = max_wal_bytes,
      .turns_purged = total_turns_purged,
      .facts_purged = total_facts_purged,
      .experiences_purged = total_experiences_purged,
      .quarantine_cleaned = total_quarantine_cleaned,
  };
}

[[nodiscard]] dasall::memory::MemoryConfig make_quality_sqlite_config(
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

[[nodiscard]] std::shared_ptr<dasall::infra::metrics::MetricsFacade>
make_metrics_facade() {
  auto facade = std::make_shared<dasall::infra::metrics::MetricsFacade>();
  assert_true(facade->init(make_provider_config()).ok,
              "memory release soak probe should initialize MetricsFacade");
  return facade;
}

[[nodiscard]] dasall::memory::MemoryWritebackRequest make_quality_request(
    const std::string& session_id,
    const std::string& turn_id,
    const std::string& user_input,
    const std::string& agent_response,
    const std::string& fact_text,
    const std::uint32_t confidence_score,
    const bool inject_invalid_fact) {
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

[[nodiscard]] RollingRateStats run_recall_metric_probe();

[[nodiscard]] RollingRateStats read_rate_metric(
  const dasall::infra::metrics::AggregationSnapshot& snapshot,
    const std::string& metric_name) {
  const auto* metric = snapshot.find(metric_name);
  assert_true(metric != nullptr,
              "memory release soak probe expected a quality metric sample");
  return RollingRateStats{
      .sample_count = metric->sample_count,
      .min_value = metric->min_value,
      .max_value = metric->max_value,
      .last_value = metric->last_value,
  };
}

[[nodiscard]] QualityMetricSample run_quality_metric_probe() {
  const auto database_path =
      make_temp_database_path("dasall-memory-release-soak-quality");
  cleanup_database_artifacts(database_path);

  auto metrics_facade = make_metrics_facade();
  auto llm_manager = std::make_shared<MockLLMManager>();
  const auto config = make_quality_sqlite_config(database_path);
  dasall::memory::MemoryRuntimeDependencies runtime_dependencies;
  runtime_dependencies.metrics_provider = metrics_facade;
  runtime_dependencies.summarizer_factory =
      [llm_manager](const dasall::memory::MemoryConfig&) {
        return std::make_unique<LLMBackedSummarizer>(llm_manager);
      };
  runtime_dependencies.profile_id = "desktop_full";
  auto manager = dasall::memory::create_memory_manager(config, runtime_dependencies);

  const auto init_code = manager->init(config);
  assert_true(static_cast<int>(init_code) == 0,
              "memory release soak probe should initialize the quality manager");

  const auto first_writeback = manager->write_back(make_quality_request(
      "session-memory-release-soak-quality",
      "turn-memory-release-soak-quality-001",
      "network mode enabled",
      "retry budget 3",
      "network mode enabled",
      70,
      false));
  assert_true(!first_writeback.result_code.has_value() && !first_writeback.partial,
              "memory release soak probe requires a healthy first writeback");

  const auto conflict_writeback = manager->write_back(make_quality_request(
      "session-memory-release-soak-quality",
      "turn-memory-release-soak-quality-002",
      "update network mode",
      "network mode disabled",
      "network mode disabled",
      95,
      false));
  assert_true(!conflict_writeback.result_code.has_value() &&
                  conflict_writeback.conflicts.size() == 1U,
              "memory release soak probe requires conflict precision seed data");

  const auto partial_writeback = manager->write_back(make_quality_request(
      "session-memory-release-soak-quality",
      "turn-memory-release-soak-quality-003",
      "stabilize retry budget",
      "retry budget 3 remains active",
      "retry budget 3",
      88,
      true));
  assert_true(!partial_writeback.result_code.has_value() && partial_writeback.partial,
              "memory release soak probe requires a partial writeback sample");

  struct GroundTruthCase {
    std::string request_id;
    std::vector<std::string> external_evidence;
    std::string latest_observation_digest;
    std::optional<std::string> llm_payload;
    bool expect_fallback = false;
  };

  const std::vector<GroundTruthCase> ground_truth_cases{
      GroundTruthCase{
          .request_id = "req-memory-release-soak-summarizer",
          .external_evidence = {"network mode disabled", "retry budget 3"},
          .latest_observation_digest = "retry budget 3",
          .llm_payload = std::string(
              R"({"schema_version":"memory_summary.v1","summary_text":"network mode disabled. retry budget 3.","decisions_made":[],"confirmed_facts":["network mode disabled"],"tool_outcomes":[]})"),
          .expect_fallback = false,
      },
      GroundTruthCase{
          .request_id = "req-memory-release-soak-fallback",
          .external_evidence = {"network mode disabled", "retry budget 3"},
          .latest_observation_digest = "retry budget 3",
          .llm_payload = std::nullopt,
          .expect_fallback = true,
      },
  };

  for (const auto& ground_truth : ground_truth_cases) {
    if (ground_truth.llm_payload.has_value()) {
      llm_manager->set_generate_result(
          MockLLMManager::make_structured_stage_result(
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
    context_request.session_id = "session-memory-release-soak-quality";
    context_request.stage = "reasoning";
    context_request.goal_summary =
        "measure end-to-end memory quality metrics";
    context_request.latest_observation_digest_summary =
        ground_truth.latest_observation_digest;
    context_request.token_budget_hint = 224;
    context_request.external_evidence = ground_truth.external_evidence;

    const auto context_result = manager->prepare_context(context_request);
    assert_true(!context_result.result_code.has_value() &&
                    !context_result.compression_notes.empty(),
                "memory release soak probe should produce compressed context packets");
  }

  const auto snapshot = metrics_facade->aggregation_snapshot();
  const auto writeback_partial_rate =
      read_rate_metric(snapshot, std::string(kWritebackPartialRateMetricName));
  const auto summary_fallback_rate =
      read_rate_metric(snapshot, std::string(kCompressionFallbackRateMetricName));

  assert_true(writeback_partial_rate.sample_count == 3U &&
                  writeback_partial_rate.last_value > 0.0 &&
                  writeback_partial_rate.last_value < 1.0,
              "memory release soak probe should expose a bounded partial writeback rate");
  assert_true(summary_fallback_rate.sample_count == 2U &&
                  summary_fallback_rate.max_value >= 0.5,
              "memory release soak probe should expose a rolling summary fallback rate");

  manager->shutdown();
  cleanup_database_artifacts(database_path);

    const auto retrieval_recall_at_k = run_recall_metric_probe();
    assert_true(retrieval_recall_at_k.sample_count == 2U &&
            retrieval_recall_at_k.max_value > 0.99 &&
            retrieval_recall_at_k.min_value < 1.0 &&
            retrieval_recall_at_k.last_value < 1.0,
          "memory release soak probe should expose a varying retrieval recall metric");

  return QualityMetricSample{
      .writeback_partial_rate = writeback_partial_rate,
      .summary_fallback_rate = summary_fallback_rate,
      .retrieval_recall_at_k = retrieval_recall_at_k,
  };
}

  [[nodiscard]] RollingRateStats run_recall_metric_probe() {
    const auto database_path =
      make_temp_database_path("dasall-memory-release-soak-recall");
    cleanup_database_artifacts(database_path);

    auto metrics_facade = make_metrics_facade();
    auto config = make_quality_sqlite_config(database_path);
    config.context.compression_trigger_turns = 8;
    config.context.compression_trigger_ratio = 0.90;
    dasall::memory::MemoryRuntimeDependencies runtime_dependencies;
    runtime_dependencies.metrics_provider = metrics_facade;
    runtime_dependencies.profile_id = "desktop_full";
    auto manager = dasall::memory::create_memory_manager(config, runtime_dependencies);

    const auto init_code = manager->init(config);
    assert_true(static_cast<int>(init_code) == 0,
          "memory release soak recall probe should initialize the sqlite-backed manager");

    const std::vector<std::string> evidence = {
      "alpha quality evidence anchor retained in retrieval evidence with extra budget pressure tokens",
      "beta quality evidence anchor retained in retrieval evidence with extra budget pressure tokens",
      "gamma quality evidence anchor retained in retrieval evidence with extra budget pressure tokens",
    };

    dasall::memory::MemoryContextRequest full_budget_request;
    full_budget_request.request_id = "req-memory-release-soak-recall-full";
    full_budget_request.session_id = "session-memory-release-soak-recall";
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
            full_budget_result.context_packet.retrieval_evidence->size() >=
              evidence.size(),
          "memory release soak recall probe should keep all curated evidence under the relaxed budget");

    dasall::memory::MemoryContextRequest tight_budget_request;
    tight_budget_request.request_id = "req-memory-release-soak-recall-tight";
    tight_budget_request.session_id = "session-memory-release-soak-recall";
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
            tight_budget_result.context_packet.retrieval_evidence->size() <
              evidence.size(),
          "memory release soak recall probe should trim curated evidence under the tight budget");

    const auto snapshot = metrics_facade->aggregation_snapshot();
    const auto retrieval_recall_at_k =
      read_rate_metric(snapshot, std::string(kRecallAtKMetricName));

    manager->shutdown();
    cleanup_database_artifacts(database_path);
    return retrieval_recall_at_k;
  }

[[nodiscard]] float dot_product(const std::vector<float>& left,
                                const std::vector<float>& right) {
  if (left.size() != right.size()) {
    return 0.0F;
  }

  float value = 0.0F;
  for (std::size_t index = 0U; index < left.size(); ++index) {
    value += left[index] * right[index];
  }
  return value;
}

[[nodiscard]] RecallFixture select_baseline_miss_fixture() {
  const std::vector<std::string> relevant_candidates = {
      "espresso",
      "cappuccino",
      "arabica",
      "macchiato",
  };
  const std::vector<std::string> distractor_candidates = {
      "sencha",
      "oolong",
      "matcha",
      "earlgrey",
  };
  const std::vector<std::string> query_candidates = {
      "latte",
      "americano",
      "flatwhite",
      "ristretto",
  };

  const dasall::memory::SimpleLocalEmbeddingAdapter baseline_adapter;
  for (const auto& query_text : query_candidates) {
    const auto query_embedding = baseline_adapter.embed(query_text);
    if (query_embedding.empty()) {
      continue;
    }

    for (const auto& relevant_doc_text : relevant_candidates) {
      const auto relevant_embedding = baseline_adapter.embed(relevant_doc_text);
      if (relevant_embedding.empty()) {
        continue;
      }
      const float relevant_score = dot_product(query_embedding, relevant_embedding);

      for (const auto& distractor_doc_text : distractor_candidates) {
        const auto distractor_embedding =
            baseline_adapter.embed(distractor_doc_text);
        if (distractor_embedding.empty()) {
          continue;
        }

        const float distractor_score =
            dot_product(query_embedding, distractor_embedding);
        if (relevant_score <= distractor_score) {
          return RecallFixture{
              .query_text = query_text,
              .relevant_doc_text = relevant_doc_text,
              .distractor_doc_text = distractor_doc_text,
          };
        }
      }
    }
  }

  throw std::runtime_error(
      "memory release soak probe could not find a deterministic vector recall fixture");
}

class SemanticEmbeddingTransport final : public dasall::llm::ILLMTransport {
 public:
  explicit SemanticEmbeddingTransport(RecallFixture fixture)
      : fixture_(std::move(fixture)) {}

  [[nodiscard]] dasall::llm::LLMTransportResponse send(
      const dasall::llm::LLMTransportRequest& request) override {
    ++send_calls_;

    const std::vector<float> embedding = classify_embedding(request.body);
    std::string body = "{\"data\":[{\"embedding\":[";
    for (std::size_t index = 0U; index < embedding.size(); ++index) {
      if (index != 0U) {
        body += ',';
      }
      body += std::to_string(embedding[index]);
    }
    body += "],\"index\":0}],\"model\":\"semantic-test\"}";

    return dasall::llm::LLMTransportResponse{
        .status_code = 200U,
        .body = std::move(body),
        .error_message = {},
    };
  }

  int send_calls_ = 0;

 private:
  [[nodiscard]] std::vector<float> classify_embedding(
      const std::string& payload) const {
    if (payload.find(fixture_.query_text) != std::string::npos ||
        payload.find(fixture_.relevant_doc_text) != std::string::npos) {
      return {1.0F, 0.0F, 0.0F};
    }

    if (payload.find(fixture_.distractor_doc_text) != std::string::npos) {
      return {0.0F, 1.0F, 0.0F};
    }

    return {0.0F, 0.0F, 1.0F};
  }

  RecallFixture fixture_;
};

class ScoringSqliteVssDriver final
    : public dasall::memory::SqliteVssVectorBackend::Driver {
 public:
  [[nodiscard]] bool reports_available() const override {
    return true;
  }

  [[nodiscard]] dasall::memory::StoreResult initialize(
      sqlite3* db,
      const int embedding_dimension) override {
    if (db == nullptr || embedding_dimension <= 0) {
      return dasall::memory::StoreResult::failure(
          dasall::contracts::ResultCode::RuntimeRetryExhausted,
          "scoring driver requires sqlite handle and a positive embedding dimension");
    }

    embedding_dimension_ = embedding_dimension;
    return dasall::memory::StoreResult::success("scoring-driver-init");
  }

  [[nodiscard]] dasall::memory::StoreResult upsert(
      sqlite3* db,
      const dasall::memory::VectorDocument& document,
      const std::vector<float>& embedding) override {
    if (db == nullptr || embedding.empty()) {
      return dasall::memory::StoreResult::failure(
          dasall::contracts::ResultCode::RuntimeRetryExhausted,
          "scoring driver requires sqlite handle and a non-empty embedding");
    }

    const auto existing = std::find_if(
        entries_.begin(),
        entries_.end(),
        [&document](const auto& entry) {
          return entry.document.doc_id == document.doc_id;
        });
    if (existing != entries_.end()) {
      existing->document = document;
      existing->embedding = embedding;
      return dasall::memory::StoreResult::success(document.doc_id);
    }

    entries_.push_back(StoredEntry{.document = document, .embedding = embedding});
    return dasall::memory::StoreResult::success(document.doc_id);
  }

  [[nodiscard]] std::vector<dasall::memory::VectorHit> search(
      sqlite3* db,
      const std::vector<float>& query_embedding,
      const int top_k) const override {
    if (db == nullptr || query_embedding.empty() || top_k <= 0) {
      return {};
    }

    std::vector<ScoredHit> scored_hits;
    scored_hits.reserve(entries_.size());
    for (std::size_t index = 0U; index < entries_.size(); ++index) {
      const float score =
          dot_product(query_embedding, entries_[index].embedding);
      scored_hits.push_back(ScoredHit{
          .score = score,
          .hit = dasall::memory::VectorHit{
              .doc_id = entries_[index].document.doc_id,
              .doc_type = entries_[index].document.doc_type,
              .score = score,
              .text_snippet = entries_[index].document.text,
          },
      });
    }

    std::stable_sort(scored_hits.begin(), scored_hits.end(),
                     [](const ScoredHit& left, const ScoredHit& right) {
                       return left.score > right.score;
                     });

    std::vector<dasall::memory::VectorHit> hits;
    const std::size_t result_count =
        std::min<std::size_t>(scored_hits.size(),
                              static_cast<std::size_t>(top_k));
    hits.reserve(result_count);
    for (std::size_t index = 0U; index < result_count; ++index) {
      hits.push_back(scored_hits[index].hit);
    }
    return hits;
  }

  [[nodiscard]] int indexed_doc_count(sqlite3*) const override {
    return static_cast<int>(entries_.size());
  }

  [[nodiscard]] dasall::memory::StoreResult rebuild_index(sqlite3* db) override {
    if (db == nullptr || embedding_dimension_ <= 0) {
      return dasall::memory::StoreResult::failure(
          dasall::contracts::ResultCode::RuntimeRetryExhausted,
          "scoring driver rebuild requires initialization");
    }

    return dasall::memory::StoreResult::success("scoring-driver-rebuild");
  }

 private:
  struct StoredEntry {
    dasall::memory::VectorDocument document;
    std::vector<float> embedding;
  };

  struct ScoredHit {
    float score = 0.0F;
    dasall::memory::VectorHit hit;
  };

  std::vector<StoredEntry> entries_;
  int embedding_dimension_ = 0;
};

using SqliteHandle = std::unique_ptr<sqlite3, decltype(&sqlite3_close)>;

[[nodiscard]] SqliteHandle open_in_memory_database() {
  sqlite3* db = nullptr;
  if (sqlite3_open(":memory:", &db) != SQLITE_OK) {
    const auto error_message =
        db == nullptr ? std::string{"sqlite open failed"}
                      : std::string{sqlite3_errmsg(db)};
    if (db != nullptr) {
      sqlite3_close(db);
    }
    throw std::runtime_error(error_message);
  }

  return SqliteHandle{db, &sqlite3_close};
}

[[nodiscard]] double recall_at_1(const std::vector<dasall::memory::VectorHit>& hits,
                                 const std::string& expected_doc_id) {
  if (hits.empty()) {
    return 0.0;
  }
  return hits.front().doc_id == expected_doc_id ? 1.0 : 0.0;
}

void upsert_fixture_documents(dasall::memory::SqliteVssVectorBackend& backend,
                              const RecallFixture& fixture) {
  const auto distractor_result = backend.upsert(dasall::memory::VectorDocument{
      .doc_id = "tea-doc",
      .doc_type = "fact",
      .text = fixture.distractor_doc_text,
      .embedding = {},
  });
  assert_true(distractor_result.ok,
              "memory release soak probe should upsert the distractor document");

  const auto relevant_result = backend.upsert(dasall::memory::VectorDocument{
      .doc_id = "coffee-doc",
      .doc_type = "fact",
      .text = fixture.relevant_doc_text,
      .embedding = {},
  });
  assert_true(relevant_result.ok,
              "memory release soak probe should upsert the relevant document");
}

[[nodiscard]] VectorRecallSample run_vector_recall_probe() {
  const auto fixture = select_baseline_miss_fixture();

  dasall::memory::VectorConfig config;
  config.enabled = true;
  config.backend_type = dasall::memory::VectorBackend::SqliteVss;

  auto local_db = open_in_memory_database();
  dasall::memory::SimpleLocalEmbeddingAdapter local_adapter;
  auto local_backend = dasall::memory::SqliteVssVectorBackend(
      config,
      local_db.get(),
      &local_adapter,
      std::make_unique<ScoringSqliteVssDriver>());
  upsert_fixture_documents(local_backend, fixture);
  const auto local_hits = local_backend.search(fixture.query_text, kVectorRecallK);
  const double local_recall = recall_at_1(local_hits, "coffee-doc");

  auto semantic_db = open_in_memory_database();
  auto transport = std::make_shared<SemanticEmbeddingTransport>(fixture);
  dasall::apps::runtime_support::LLMBackedEmbeddingAdapter::Options options;
  options.provider =
      dasall::apps::runtime_support::LLMBackedEmbeddingAdapter::ProviderConfig{
          .provider_id = "semantic-test",
          .model_id = "semantic-embedding",
          .base_url = "https://embedding.example/v1",
          .auth_ref = "profile://embedding.default",
          .base_url_alias = "semantic.test",
          .snapshot_version = "semantic-test@2026.06.18",
          .timeout_ms = 5000U,
      };
  options.composition_owner = "memory.release-soak.vector-recall";
  dasall::apps::runtime_support::LLMBackedEmbeddingAdapter semantic_adapter(
      transport,
      nullptr,
      std::move(options));
  auto semantic_backend = dasall::memory::SqliteVssVectorBackend(
      config,
      semantic_db.get(),
      &semantic_adapter,
      std::make_unique<ScoringSqliteVssDriver>());
  upsert_fixture_documents(semantic_backend, fixture);
  const auto semantic_hits =
      semantic_backend.search(fixture.query_text, kVectorRecallK);
  const double semantic_recall = recall_at_1(semantic_hits, "coffee-doc");

  assert_true(local_recall == 0.0,
              "memory release soak probe should prove the local hash baseline misses the semantic fixture");
  assert_true(semantic_recall == 1.0,
              "memory release soak probe should recover the relevant document after semantic embedding injection");
  assert_true(semantic_recall > local_recall,
              "memory release soak probe should show vector recall improvement");

  return VectorRecallSample{
      .k = kVectorRecallK,
      .local_baseline = local_recall,
      .semantic_adapter = semantic_recall,
      .provider_calls = transport->send_calls_,
  };
}

[[nodiscard]] std::optional<fs::path> parse_artifact_dir(
    const int argc,
    char** argv) {
  std::optional<fs::path> artifact_dir;
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--artifact-dir") {
      if (index + 1 >= argc) {
        throw std::runtime_error("--artifact-dir requires a value");
      }
      artifact_dir = fs::path(argv[++index]);
      continue;
    }

    if (argument == "-h" || argument == "--help") {
      std::cout << "Usage: MemoryReleaseSoakProbeTest [--artifact-dir <path>]\n";
      std::exit(0);
    }

    throw std::runtime_error("unknown argument: " + argument);
  }
  return artifact_dir;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const auto artifact_dir = parse_artifact_dir(argc, argv);
    const ReleaseSoakSummary summary{
        .long_running = run_long_running_soak_probe(),
        .quality = run_quality_metric_probe(),
        .vector_recall = run_vector_recall_probe(),
    };

    if (artifact_dir.has_value()) {
      write_summary_json(summary, *artifact_dir / kSummaryFileName);
    }

    std::cout << "MemoryReleaseSoakProbeTest: PASS\n";
    return 0;
  } catch (const std::exception& exception) {
    std::cerr << "[MemoryReleaseSoakProbeTest] FAILED: " << exception.what()
              << '\n';
    return 1;
  }
}