#include "config/MemoryConfigProjector.h"

#include <algorithm>

namespace dasall::memory::config {
namespace {

template <typename T>
[[nodiscard]] T clamp_value(T value, T lower, T upper) {
  return std::clamp(value, lower, upper);
}

}  // namespace

std::optional<MemoryConfig> project_memory_config(
    const profiles::RuntimePolicySnapshot& snapshot,
    const profiles::BuildProfileManifest& manifest) {
  if (!snapshot.has_consistent_values() || !manifest.has_consistent_values()) {
    return std::nullopt;
  }

  const auto max_turns = snapshot.runtime_budget().max_turns.value_or(0U);
  const auto max_input_tokens = snapshot.token_budget_policy().max_input_tokens;
  const auto max_history_turns = snapshot.token_budget_policy().max_history_turns;
  const auto compression_threshold = snapshot.token_budget_policy().compression_threshold;
  if (max_turns == 0U || max_input_tokens == 0U || max_history_turns == 0U ||
      compression_threshold == 0U) {
    return std::nullopt;
  }

  const bool vector_enabled = manifest.enables_module("memory_vector");
  const auto recent_turn_limit =
      static_cast<int>(std::min(max_turns, max_history_turns));
  const auto compression_trigger_turns = std::max(
      4,
      recent_turn_limit - std::max(1, recent_turn_limit / 4));
  const auto max_summary_candidates =
      !vector_enabled ? 1 : (snapshot.worker_threads() >= 8U ? 3 : 2);
  const auto wal_autocheckpoint_pages = clamp_value(
      static_cast<int>(max_turns * 32U),
      200,
      1000);
  const auto reader_pool_size = clamp_value(
      static_cast<int>(snapshot.worker_threads() / 2U),
      1,
      4);
  const auto retention_turns = clamp_value(
      static_cast<int>(max_turns) * 20,
      120,
      480);
  const auto schedule_interval_ms = snapshot.worker_threads() >= 8U
                                        ? 60000
                                        : (snapshot.worker_threads() >= 4U ? 90000 : 120000);
  const auto busy_timeout_ms =
      snapshot.capability_cache_policy().stale_read_allowed ? 75 : 50;
  const auto search_top_k = !vector_enabled ? 0 : (snapshot.worker_threads() >= 8U ? 8 : 5);
  const auto compression_trigger_ratio = clamp_value(
      static_cast<double>(compression_threshold) /
          static_cast<double>(max_input_tokens),
      0.50,
      0.90);

  MemoryConfig config;
  config.storage.backend = StorageBackend::Sqlite;
  config.storage.journal_mode = JournalMode::Wal;
  config.storage.synchronous = SynchronousMode::Normal;
  config.storage.wal_autocheckpoint_pages = wal_autocheckpoint_pages;
  config.storage.busy_timeout_ms = busy_timeout_ms;
  config.storage.writer_retry_count = snapshot.degrade_policy().allow_budget_degrade ? 2 : 1;
    config.storage.sqlite_min_version = encode_sqlite_version_number(3, 51, 3);
  config.storage.checkpoint_mode = CheckpointMode::Passive;
  config.storage.reader_pool_size = reader_pool_size;

  config.context.recent_turn_limit = recent_turn_limit;
  config.context.compression_trigger_turns = compression_trigger_turns;
  config.context.max_summary_candidates = max_summary_candidates;
  config.context.fact_confidence_floor =
      manifest.enables_module("memory_experience") ? 80 : 90;
  config.context.compression_trigger_ratio = compression_trigger_ratio;

  config.experience.effectiveness_floor =
      manifest.enables_module("memory_experience") ? 60 : 100;

  config.vector.enabled = vector_enabled;
  config.vector.backend_type = vector_enabled ? VectorBackend::SqliteVss : VectorBackend::None;
  config.vector.search_top_k = search_top_k;
    config.token_estimator = TokenEstimatorBackend::Tiktoken;

  config.maintenance.retention_turns = retention_turns;
  config.maintenance.quarantine_enabled = true;
  config.maintenance.auto_schedule = snapshot.worker_threads() >= 4U;
  config.maintenance.schedule_interval_ms = schedule_interval_ms;

  return config;
}

}  // namespace dasall::memory::config