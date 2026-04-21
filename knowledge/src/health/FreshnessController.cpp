#include "health/FreshnessController.h"

#include <algorithm>

namespace dasall::knowledge {

namespace {

void append_reason_code(std::vector<std::string>& reason_codes, std::string reason_code) {
  if (std::find(reason_codes.begin(), reason_codes.end(), reason_code) == reason_codes.end()) {
    reason_codes.push_back(std::move(reason_code));
  }
}

[[nodiscard]] FreshnessSnapshot make_unknown_snapshot(std::string reason_code) {
  FreshnessSnapshot snapshot;
  snapshot.state = FreshnessState::Unknown;
  snapshot.rebuild_recommended = true;
  append_reason_code(snapshot.reason_codes, std::move(reason_code));
  return snapshot;
}

}  // namespace

bool IndexManifest::has_consistent_values() const {
  return format_version > 0U && !lexical_backend.empty() && !tokenizer_profile.empty() &&
         !snapshot_id.empty() && built_at > 0 && effective_at >= built_at &&
         chunk_count >= document_count;
}

bool FreshnessSnapshot::has_consistent_values() const {
  if (age_ms < 0 || !detail::has_unique_values(reason_codes)) {
    return false;
  }

  switch (state) {
    case FreshnessState::Fresh:
      return !stale_read_allowed && !rebuild_recommended;
    case FreshnessState::StaleAllowed:
      return stale_read_allowed && rebuild_recommended;
    case FreshnessState::StaleRejected:
      return !stale_read_allowed && rebuild_recommended;
    case FreshnessState::Unknown:
      return !stale_read_allowed;
  }

  return false;
}

FreshnessSnapshot FreshnessController::evaluate(const std::optional<IndexManifest>& manifest,
                                               const KnowledgeConfigSnapshot& config,
                                               std::int64_t now_ms,
                                               bool query_allow_stale) const {
  if (!config.has_consistent_values()) {
    return make_unknown_snapshot("config_inconsistent");
  }

  if (!manifest.has_value()) {
    return make_unknown_snapshot("manifest_missing");
  }

  if (!manifest->has_consistent_values() || now_ms < manifest->effective_at) {
    return make_unknown_snapshot("manifest_timestamp_invalid");
  }

  FreshnessSnapshot snapshot;
  snapshot.age_ms = now_ms - manifest->effective_at;

  if (snapshot.age_ms <= config.catalog_refresh_interval_ms) {
    snapshot.state = FreshnessState::Fresh;
    append_reason_code(snapshot.reason_codes, "within_refresh_interval");
    return snapshot;
  }

  snapshot.rebuild_recommended = true;

  if (snapshot.age_ms > config.catalog_expire_after_ms) {
    snapshot.state = FreshnessState::StaleRejected;
    append_reason_code(snapshot.reason_codes, "catalog_expired");
    return snapshot;
  }

  append_reason_code(snapshot.reason_codes, "refresh_interval_elapsed");

  if (config.allow_stale_read && query_allow_stale) {
    snapshot.state = FreshnessState::StaleAllowed;
    snapshot.stale_read_allowed = true;
    append_reason_code(snapshot.reason_codes, "stale_read_allowed");
    return snapshot;
  }

  snapshot.state = FreshnessState::StaleRejected;
  if (!config.allow_stale_read) {
    append_reason_code(snapshot.reason_codes, "profile_stale_read_disabled");
  }
  if (!query_allow_stale) {
    append_reason_code(snapshot.reason_codes, "query_stale_opt_in_missing");
  }

  return snapshot;
}

}  // namespace dasall::knowledge