#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "KnowledgeTypes.h"

namespace dasall::knowledge {

struct KnowledgeTelemetryEvent {
  std::string event_name;
  std::string request_id;
  std::string component;
  std::string snapshot_id;
  std::string result;
  bool degraded = false;
  std::int64_t latency_ms = 0;
  std::vector<std::string> reason_codes;
  std::vector<std::string> corpus_ids;
  std::string profile_id;
  std::optional<KnowledgeQueryKind> query_kind;
  std::optional<RetrievalMode> retrieval_mode;
  std::size_t corpus_count = 0U;
  std::size_t result_count = 0U;
  std::string error_category = "none";

  [[nodiscard]] bool has_consistent_values() const;
};

using KnowledgeTelemetrySink = std::function<void(const KnowledgeTelemetryEvent&)>;

struct TelemetrySinks {
  KnowledgeTelemetrySink log_sink;
  KnowledgeTelemetrySink metrics_sink;
  KnowledgeTelemetrySink trace_sink;
  KnowledgeTelemetrySink audit_sink;
  KnowledgeTelemetrySink fallback_log_sink;
};

struct KnowledgeTelemetryStatus {
  std::uint64_t retrieve_event_total = 0U;
  std::uint64_t ingest_event_total = 0U;
  std::uint64_t health_event_total = 0U;
  std::uint64_t snapshot_swap_event_total = 0U;
  std::uint64_t dropped_event_total = 0U;
  std::uint64_t invalid_payload_total = 0U;
  std::uint64_t sink_failure_total = 0U;
  std::uint64_t fallback_log_total = 0U;
  bool degraded = false;

  [[nodiscard]] bool has_consistent_values() const {
    return fallback_log_total <= invalid_payload_total + sink_failure_total &&
           dropped_event_total >= invalid_payload_total;
  }
};

class KnowledgeTelemetry {
 public:
  explicit KnowledgeTelemetry(TelemetrySinks sinks);

  void emit_retrieve_event(const KnowledgeTelemetryEvent& event) const;
  void emit_ingest_event(const KnowledgeTelemetryEvent& event) const;
  void emit_health_event(const KnowledgeTelemetryEvent& event) const;
  void emit_snapshot_swap_event(const KnowledgeTelemetryEvent& event) const;

  [[nodiscard]] KnowledgeTelemetryStatus status() const;

 private:
  enum class EventKind : std::uint8_t {
    Retrieve = 0,
    Ingest = 1,
    Health = 2,
    SnapshotSwap = 3,
  };

  void emit_event(EventKind kind, const KnowledgeTelemetryEvent& event) const;
  [[nodiscard]] static bool has_required_fields(EventKind kind,
                                                const KnowledgeTelemetryEvent& event);
  [[nodiscard]] static KnowledgeTelemetryEvent make_invalid_payload_event(
      const KnowledgeTelemetryEvent& original,
      std::string reason_code);
  void record_kind(EventKind kind) const;
  void record_sink_failure(const KnowledgeTelemetryEvent& event,
                           std::string reason_code) const;
  void emit_fallback_log(KnowledgeTelemetryEvent event) const;

  TelemetrySinks sinks_;
  mutable std::mutex mutex_;
  mutable KnowledgeTelemetryStatus status_{};
};

}  // namespace dasall::knowledge