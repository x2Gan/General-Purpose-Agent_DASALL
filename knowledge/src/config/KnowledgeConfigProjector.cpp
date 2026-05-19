#include "config/KnowledgeConfigProjector.h"

#include <algorithm>
#include <string_view>

#include "BuildProfileManifest.h"
#include "RuntimePolicySnapshot.h"

namespace dasall::knowledge::config {
namespace {

template <typename T>
[[nodiscard]] T clamp_value(T value, T lower, T upper) {
  return std::clamp(value, lower, upper);
}

[[nodiscard]] bool is_edge_like_profile(std::string_view profile_id) {
  return profile_id.starts_with("edge_") || profile_id == "factory_test";
}

[[nodiscard]] std::size_t derive_evidence_budget_tokens(
    const profiles::RuntimePolicySnapshot& snapshot) {
  const auto max_input_tokens =
      static_cast<std::size_t>(snapshot.token_budget_policy().max_input_tokens);
  const auto compression_threshold =
      static_cast<std::size_t>(snapshot.token_budget_policy().compression_threshold);

  return std::max<std::size_t>(
      1U,
      std::min(max_input_tokens / 4U, compression_threshold / 2U));
}

[[nodiscard]] std::size_t derive_max_context_projection_items(
    const profiles::RuntimePolicySnapshot& snapshot) {
  if (snapshot.worker_threads() >= 8U) {
    return 8U;
  }

  if (snapshot.worker_threads() >= 4U) {
    return 6U;
  }

  return 4U;
}

[[nodiscard]] std::int64_t derive_request_deadline_ms(
    const profiles::RuntimePolicySnapshot& snapshot) {
  const auto max_latency_ms =
      static_cast<std::int64_t>(snapshot.runtime_budget().max_latency_ms.value_or(0U));
  return clamp_value<std::int64_t>(max_latency_ms / 3, 300, 1500);
}

[[nodiscard]] std::size_t derive_max_parallel_recall(
    const profiles::RuntimePolicySnapshot& snapshot) {
  return std::min<std::size_t>(2U, std::max<std::size_t>(1U, snapshot.worker_threads() / 2U));
}

[[nodiscard]] std::int64_t derive_lane_timeout_ms(std::int64_t request_deadline_ms) {
  return std::max<std::int64_t>(1, request_deadline_ms * 35 / 100);
}

[[nodiscard]] std::int64_t derive_ingest_timeout_ms(
    const profiles::RuntimePolicySnapshot& snapshot) {
  return is_edge_like_profile(snapshot.effective_profile_id()) ? 10000 : 30000;
}

}  // namespace

bool KnowledgeConfigProjectorOverlay::has_consistent_values() const {
  if (evidence_budget_tokens.has_value() && *evidence_budget_tokens == 0U) {
    return false;
  }

  if (max_context_projection_items.has_value() && *max_context_projection_items == 0U) {
    return false;
  }

  if (catalog_refresh_interval_ms.has_value() && *catalog_refresh_interval_ms <= 0) {
    return false;
  }

  if (catalog_expire_after_ms.has_value() && *catalog_expire_after_ms <= 0) {
    return false;
  }

  if (catalog_refresh_interval_ms.has_value() && catalog_expire_after_ms.has_value() &&
      *catalog_expire_after_ms < *catalog_refresh_interval_ms) {
    return false;
  }

  if (failure_backoff_ms.has_value() && *failure_backoff_ms < 0) {
    return false;
  }

  if (request_deadline_ms.has_value() && *request_deadline_ms <= 0) {
    return false;
  }

  if (max_parallel_recall.has_value() && *max_parallel_recall == 0U) {
    return false;
  }

  if (sparse_recall_timeout_ms.has_value() && *sparse_recall_timeout_ms <= 0) {
    return false;
  }

  if (dense_recall_timeout_ms.has_value() && *dense_recall_timeout_ms <= 0) {
    return false;
  }

  if (ingest_timeout_ms.has_value() && *ingest_timeout_ms <= 0) {
    return false;
  }

  return true;
}

std::optional<KnowledgeConfigSnapshot> KnowledgeConfigProjector::project(
    const profiles::RuntimePolicySnapshot& snapshot,
    const profiles::BuildProfileManifest& manifest,
    const KnowledgeConfigProjectorOverlay& overlay) {
  if (!snapshot.has_consistent_values() || !manifest.has_consistent_values() ||
      !overlay.has_consistent_values()) {
    return std::nullopt;
  }

  if (!snapshot.runtime_budget().max_latency_ms.has_value()) {
    return std::nullopt;
  }

  KnowledgeConfigSnapshot config;
  config.knowledge_enabled = manifest.enables_module("knowledge");
  config.vector_enabled = manifest.enables_module("memory_vector");
  config.retrieval_mode_default = RetrievalMode::LexicalOnly;
  config.profile_id = snapshot.effective_profile_id();
  config.evidence_budget_tokens = derive_evidence_budget_tokens(snapshot);
  config.max_context_projection_items = derive_max_context_projection_items(snapshot);
  config.catalog_refresh_interval_ms = snapshot.capability_cache_policy().refresh_interval_ms;
  config.catalog_expire_after_ms = snapshot.capability_cache_policy().expire_after_ms;
  config.allow_stale_read = snapshot.capability_cache_policy().stale_read_allowed;
  config.failure_backoff_ms = snapshot.capability_cache_policy().failure_backoff_ms;
  config.request_deadline_ms = derive_request_deadline_ms(snapshot);
  config.allow_budget_degrade = snapshot.degrade_policy().allow_budget_degrade;
  config.max_parallel_recall = derive_max_parallel_recall(snapshot);
  config.ingest_timeout_ms = derive_ingest_timeout_ms(snapshot);

  if (overlay.retrieval_mode_default.has_value()) {
    config.retrieval_mode_default = *overlay.retrieval_mode_default;
  }
  if (overlay.evidence_budget_tokens.has_value()) {
    config.evidence_budget_tokens = *overlay.evidence_budget_tokens;
  }
  if (overlay.max_context_projection_items.has_value()) {
    config.max_context_projection_items = *overlay.max_context_projection_items;
  }
  if (overlay.catalog_refresh_interval_ms.has_value()) {
    config.catalog_refresh_interval_ms = *overlay.catalog_refresh_interval_ms;
  }
  if (overlay.catalog_expire_after_ms.has_value()) {
    config.catalog_expire_after_ms = *overlay.catalog_expire_after_ms;
  }
  if (overlay.allow_stale_read.has_value()) {
    config.allow_stale_read = *overlay.allow_stale_read;
  }
  if (overlay.failure_backoff_ms.has_value()) {
    config.failure_backoff_ms = *overlay.failure_backoff_ms;
  }
  if (overlay.request_deadline_ms.has_value()) {
    config.request_deadline_ms = *overlay.request_deadline_ms;
  }
  if (overlay.allow_budget_degrade.has_value()) {
    config.allow_budget_degrade = *overlay.allow_budget_degrade;
  }
  if (overlay.max_parallel_recall.has_value()) {
    config.max_parallel_recall = *overlay.max_parallel_recall;
  }
  if (overlay.ingest_timeout_ms.has_value()) {
    config.ingest_timeout_ms = *overlay.ingest_timeout_ms;
  }

  config.sparse_recall_timeout_ms = overlay.sparse_recall_timeout_ms.value_or(
      derive_lane_timeout_ms(config.request_deadline_ms));
  config.dense_recall_timeout_ms = overlay.dense_recall_timeout_ms.value_or(
      derive_lane_timeout_ms(config.request_deadline_ms));

  if (!config.has_consistent_values()) {
    return std::nullopt;
  }

  return config;
}

}  // namespace dasall::knowledge::config