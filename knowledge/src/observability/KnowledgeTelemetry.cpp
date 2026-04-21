#include "health/KnowledgeTelemetry.h"

#include <algorithm>
#include <array>
#include <utility>

namespace dasall::knowledge {
namespace {

[[nodiscard]] bool has_unique_values(const std::vector<std::string>& values) {
  std::vector<std::string> sorted_values = values;
  std::sort(sorted_values.begin(), sorted_values.end());
  return std::adjacent_find(sorted_values.begin(), sorted_values.end()) == sorted_values.end();
}

void append_reason_code(std::vector<std::string>& reason_codes, std::string reason_code) {
  if (std::find(reason_codes.begin(), reason_codes.end(), reason_code) == reason_codes.end()) {
    reason_codes.push_back(std::move(reason_code));
  }
}

[[nodiscard]] std::string sink_failure_reason(std::string_view sink_name) {
  return std::string(sink_name) + "_sink_failure";
}

}  // namespace

bool KnowledgeTelemetryEvent::has_consistent_values() const {
  return !event_name.empty() && !component.empty() && !snapshot_id.empty() && !result.empty() &&
         latency_ms >= 0 && has_unique_values(reason_codes) && has_unique_values(corpus_ids) &&
         corpus_count == corpus_ids.size() && !error_category.empty();
}

KnowledgeTelemetry::KnowledgeTelemetry(TelemetrySinks sinks)
    : sinks_(std::move(sinks)) {}

void KnowledgeTelemetry::emit_retrieve_event(const KnowledgeTelemetryEvent& event) const {
  emit_event(EventKind::Retrieve, event);
}

void KnowledgeTelemetry::emit_ingest_event(const KnowledgeTelemetryEvent& event) const {
  emit_event(EventKind::Ingest, event);
}

void KnowledgeTelemetry::emit_health_event(const KnowledgeTelemetryEvent& event) const {
  emit_event(EventKind::Health, event);
}

void KnowledgeTelemetry::emit_snapshot_swap_event(const KnowledgeTelemetryEvent& event) const {
  emit_event(EventKind::SnapshotSwap, event);
}

KnowledgeTelemetryStatus KnowledgeTelemetry::status() const {
  std::scoped_lock lock(mutex_);
  return status_;
}

void KnowledgeTelemetry::emit_event(EventKind kind, const KnowledgeTelemetryEvent& event) const {
  if (!has_required_fields(kind, event)) {
    {
      std::scoped_lock lock(mutex_);
      ++status_.invalid_payload_total;
      ++status_.dropped_event_total;
      status_.degraded = true;
    }

    emit_fallback_log(make_invalid_payload_event(event, "invalid_payload"));
    return;
  }

  record_kind(kind);

  const std::array<std::pair<std::string_view, const KnowledgeTelemetrySink*>, 4> sinks{{
      {"log", &sinks_.log_sink},
      {"metrics", &sinks_.metrics_sink},
      {"trace", &sinks_.trace_sink},
      {"audit", &sinks_.audit_sink},
  }};

  for (const auto& [sink_name, sink] : sinks) {
    if (!(*sink)) {
      continue;
    }

    try {
      (*sink)(event);
    } catch (...) {
      record_sink_failure(event, sink_failure_reason(sink_name));
    }
  }
}

bool KnowledgeTelemetry::has_required_fields(EventKind kind,
                                             const KnowledgeTelemetryEvent& event) {
  if (!event.has_consistent_values()) {
    return false;
  }

  if (kind == EventKind::Retrieve) {
    return !event.request_id.empty() && !event.profile_id.empty() &&
           event.query_kind.has_value() && event.retrieval_mode.has_value();
  }

  return true;
}

KnowledgeTelemetryEvent KnowledgeTelemetry::make_invalid_payload_event(
    const KnowledgeTelemetryEvent& original,
    std::string reason_code) {
  KnowledgeTelemetryEvent fallback_event = original;
  if (fallback_event.component.empty()) {
    fallback_event.component = "KnowledgeTelemetry";
  }
  if (fallback_event.snapshot_id.empty()) {
    fallback_event.snapshot_id = "unknown";
  }
  if (fallback_event.event_name.empty()) {
    fallback_event.event_name = "knowledge_invalid_event";
  }

  fallback_event.result = "invalid_telemetry_payload";
  fallback_event.degraded = true;
  fallback_event.error_category = "telemetry";
  append_reason_code(fallback_event.reason_codes, std::move(reason_code));
  return fallback_event;
}

void KnowledgeTelemetry::record_kind(EventKind kind) const {
  std::scoped_lock lock(mutex_);
  switch (kind) {
    case EventKind::Retrieve:
      ++status_.retrieve_event_total;
      break;
    case EventKind::Ingest:
      ++status_.ingest_event_total;
      break;
    case EventKind::Health:
      ++status_.health_event_total;
      break;
    case EventKind::SnapshotSwap:
      ++status_.snapshot_swap_event_total;
      break;
  }
}

void KnowledgeTelemetry::record_sink_failure(const KnowledgeTelemetryEvent& event,
                                             std::string reason_code) const {
  {
    std::scoped_lock lock(mutex_);
    ++status_.sink_failure_total;
    ++status_.dropped_event_total;
    status_.degraded = true;
  }

  auto fallback_event = event;
  fallback_event.degraded = true;
  append_reason_code(fallback_event.reason_codes, std::move(reason_code));
  emit_fallback_log(std::move(fallback_event));
}

void KnowledgeTelemetry::emit_fallback_log(KnowledgeTelemetryEvent event) const {
  if (!sinks_.fallback_log_sink) {
    return;
  }

  try {
    sinks_.fallback_log_sink(event);
    std::scoped_lock lock(mutex_);
    ++status_.fallback_log_total;
  } catch (...) {
    std::scoped_lock lock(mutex_);
    ++status_.sink_failure_total;
    ++status_.dropped_event_total;
    status_.degraded = true;
  }
}

}  // namespace dasall::knowledge