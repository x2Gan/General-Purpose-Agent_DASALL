#include "health/KnowledgeHealthProbe.h"

#include <algorithm>
#include <string_view>
#include <utility>

namespace dasall::knowledge {

namespace {

void append_reason_code(std::vector<std::string>& reason_codes,
                        std::string reason_code) {
  if (std::find(reason_codes.begin(), reason_codes.end(), reason_code) ==
      reason_codes.end()) {
    reason_codes.push_back(std::move(reason_code));
  }
}

void append_reason_codes(std::vector<std::string>& reason_codes,
                         const std::vector<std::string>& extra_reason_codes) {
  for (const auto& reason_code : extra_reason_codes) {
    append_reason_code(reason_codes, reason_code);
  }
}

[[nodiscard]] bool has_reason_code(const KnowledgeHealthSnapshot& snapshot,
                                   const std::string_view& reason_code) {
  return std::find(snapshot.reason_codes.begin(),
                   snapshot.reason_codes.end(),
                   reason_code) != snapshot.reason_codes.end();
}

}  // namespace

KnowledgeHealthProbe::KnowledgeHealthProbe(HealthProbeDeps deps)
    : deps_(std::move(deps)) {}

KnowledgeHealthSnapshot KnowledgeHealthProbe::collect() const {
  KnowledgeHealthSnapshot snapshot;
  bool vector_enabled_in_manifest = false;

  if (!deps_.knowledge_enabled) {
    append_reason_code(snapshot.reason_codes, "knowledge_enabled_provider_missing");
  } else if (!deps_.knowledge_enabled()) {
    append_reason_code(snapshot.reason_codes, "knowledge_disabled");
  }

  if (!deps_.lifecycle_ready) {
    append_reason_code(snapshot.reason_codes, "lifecycle_ready_provider_missing");
  } else if (!deps_.lifecycle_ready()) {
    append_reason_code(snapshot.reason_codes, "lifecycle_not_ready");
  }

  if (!deps_.active_manifest) {
    append_reason_code(snapshot.reason_codes, "manifest_provider_missing");
  } else {
    const auto manifest = deps_.active_manifest();
    if (!manifest.has_value()) {
      append_reason_code(snapshot.reason_codes, "active_snapshot_missing");
    } else if (!manifest->has_consistent_values()) {
      append_reason_code(snapshot.reason_codes, "manifest_inconsistent");
    } else {
      snapshot.active_snapshot_id = manifest->snapshot_id;
      vector_enabled_in_manifest = manifest->vector_enabled;
    }
  }

  if (!deps_.freshness_snapshot) {
    append_reason_code(snapshot.reason_codes, "freshness_provider_missing");
  } else {
    const auto freshness = deps_.freshness_snapshot();
    if (!freshness.has_consistent_values()) {
      append_reason_code(snapshot.reason_codes, "freshness_inconsistent");
    } else {
      snapshot.freshness_state = freshness.state;
      if (freshness.state != FreshnessState::Fresh) {
        append_reason_codes(snapshot.reason_codes, freshness.reason_codes);
      }
    }
  }

  if (!deps_.vector_backend_available) {
    if (!snapshot.active_snapshot_id.empty() && vector_enabled_in_manifest) {
      append_reason_code(snapshot.reason_codes, "vector_backend_provider_missing");
    } else if (!snapshot.active_snapshot_id.empty()) {
      append_reason_code(snapshot.reason_codes, "vector_backend_disabled");
    }
  } else if (!snapshot.active_snapshot_id.empty()) {
    const bool backend_available = deps_.vector_backend_available();
    snapshot.vector_backend_available = vector_enabled_in_manifest && backend_available;
    if (!vector_enabled_in_manifest) {
      append_reason_code(snapshot.reason_codes, "vector_backend_disabled");
    } else if (!backend_available) {
      append_reason_code(snapshot.reason_codes, "vector_backend_unavailable");
    }
  }

  if (!deps_.last_known_good_available) {
    append_reason_code(snapshot.reason_codes, "last_known_good_provider_missing");
  } else {
    snapshot.last_known_good_available = deps_.last_known_good_available();
  }

  if (!deps_.telemetry_status) {
    append_reason_code(snapshot.reason_codes, "telemetry_status_provider_missing");
  } else {
    const auto telemetry_status = deps_.telemetry_status();
    if (!telemetry_status.has_consistent_values()) {
      append_reason_code(snapshot.reason_codes, "telemetry_status_inconsistent");
    } else if (telemetry_status.degraded) {
      append_reason_code(snapshot.reason_codes, "telemetry_degraded");
    }
  }

  if (!deps_.degraded_return_count) {
    append_reason_code(snapshot.reason_codes,
                       "degraded_return_count_provider_missing");
  } else {
    snapshot.degraded_return_count = deps_.degraded_return_count();
  }

  if (!deps_.recent_reason_codes) {
    append_reason_code(snapshot.reason_codes, "recent_reason_codes_provider_missing");
  } else {
    append_reason_codes(snapshot.reason_codes, deps_.recent_reason_codes());
  }

  snapshot.state = classify_state(snapshot);
  return snapshot;
}

HealthState KnowledgeHealthProbe::classify_state(
    const KnowledgeHealthSnapshot& snapshot) const {
  if (has_reason_code(snapshot, "knowledge_enabled_provider_missing") ||
      has_reason_code(snapshot, "knowledge_disabled") ||
      has_reason_code(snapshot, "lifecycle_ready_provider_missing") ||
      has_reason_code(snapshot, "lifecycle_not_ready") ||
      has_reason_code(snapshot, "manifest_provider_missing") ||
      has_reason_code(snapshot, "manifest_inconsistent") ||
      has_reason_code(snapshot, "freshness_provider_missing") ||
      has_reason_code(snapshot, "freshness_inconsistent") ||
      has_reason_code(snapshot, "vector_backend_provider_missing")) {
    return HealthState::Unknown;
  }

  if (snapshot.active_snapshot_id.empty()) {
    if (has_reason_code(snapshot, "last_known_good_provider_missing")) {
      return HealthState::Unknown;
    }

    return snapshot.last_known_good_available ? HealthState::Degraded
                                              : HealthState::Unhealthy;
  }

  if (snapshot.freshness_state == FreshnessState::Unknown) {
    return HealthState::Unknown;
  }

  if (snapshot.freshness_state == FreshnessState::StaleRejected) {
    if (has_reason_code(snapshot, "last_known_good_provider_missing")) {
      return HealthState::Unknown;
    }

    return snapshot.last_known_good_available ? HealthState::Degraded
                                              : HealthState::Unhealthy;
  }

  if (!snapshot.vector_backend_available ||
      snapshot.freshness_state == FreshnessState::StaleAllowed ||
      snapshot.degraded_return_count > 0U || !snapshot.reason_codes.empty()) {
    return HealthState::Degraded;
  }

  return HealthState::Healthy;
}

}  // namespace dasall::knowledge