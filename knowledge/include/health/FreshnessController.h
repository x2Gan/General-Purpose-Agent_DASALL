#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "KnowledgeTypes.h"

namespace dasall::knowledge {

struct IndexManifest {
  std::uint32_t format_version = 1U;
  std::string lexical_backend = "sqlite_fts5";
  std::string tokenizer_profile;
  std::string snapshot_id;
  std::int64_t built_at = 0;
  std::int64_t effective_at = 0;
  std::size_t document_count = 0U;
  std::size_t chunk_count = 0U;
  bool vector_enabled = false;

  [[nodiscard]] bool has_consistent_values() const;
};

struct FreshnessSnapshot {
  FreshnessState state = FreshnessState::Unknown;
  std::int64_t age_ms = 0;
  bool stale_read_allowed = false;
  bool rebuild_recommended = false;
  std::vector<std::string> reason_codes;

  [[nodiscard]] bool has_consistent_values() const;
};

class FreshnessController {
 public:
  [[nodiscard]] FreshnessSnapshot evaluate(const std::optional<IndexManifest>& manifest,
                                           const KnowledgeConfigSnapshot& config,
                                           std::int64_t now_ms,
                                           bool query_allow_stale = false) const;
};

}  // namespace dasall::knowledge