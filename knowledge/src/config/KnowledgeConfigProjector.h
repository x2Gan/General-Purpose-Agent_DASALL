#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "KnowledgeTypes.h"

namespace dasall::profiles {
struct BuildProfileManifest;
class RuntimePolicySnapshot;
}  // namespace dasall::profiles

namespace dasall::knowledge::config {

struct KnowledgeConfigProjectorOverlay {
  std::optional<RetrievalMode> retrieval_mode_default;
  std::optional<std::size_t> evidence_budget_tokens;
  std::optional<std::size_t> max_context_projection_items;
  std::optional<std::int64_t> catalog_refresh_interval_ms;
  std::optional<std::int64_t> catalog_expire_after_ms;
  std::optional<bool> allow_stale_read;
  std::optional<std::int64_t> failure_backoff_ms;
  std::optional<std::int64_t> request_deadline_ms;
  std::optional<bool> allow_budget_degrade;
  std::optional<std::size_t> max_parallel_recall;
  std::optional<std::int64_t> sparse_recall_timeout_ms;
  std::optional<std::int64_t> dense_recall_timeout_ms;
  std::optional<std::int64_t> ingest_timeout_ms;

  [[nodiscard]] bool has_consistent_values() const;
};

class KnowledgeConfigProjector {
 public:
  [[nodiscard]] static std::optional<KnowledgeConfigSnapshot> project(
      const profiles::RuntimePolicySnapshot& snapshot,
      const profiles::BuildProfileManifest& manifest,
      const KnowledgeConfigProjectorOverlay& overlay = {});
};

}  // namespace dasall::knowledge::config