#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "health/KnowledgeTelemetry.h"
#include "support/TestAssertions.h"

namespace {

using dasall::knowledge::KnowledgeQueryKind;
using dasall::knowledge::KnowledgeTelemetry;
using dasall::knowledge::KnowledgeTelemetryEvent;
using dasall::knowledge::RetrievalMode;
using dasall::knowledge::TelemetrySinks;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] KnowledgeTelemetryEvent make_retrieve_event(std::string event_name) {
  return KnowledgeTelemetryEvent{
      .event_name = std::move(event_name),
      .request_id = "req-knowledge-1",
      .component = "KnowledgeServiceFacade",
      .snapshot_id = "snapshot-knowledge-1",
      .result = "success",
      .degraded = false,
      .latency_ms = 42,
      .reason_codes = {},
      .corpus_ids = {"adr_normative", "ssot_normative"},
      .profile_id = "desktop_full",
      .query_kind = KnowledgeQueryKind::FactLookup,
      .retrieval_mode = RetrievalMode::Hybrid,
      .corpus_count = 2U,
      .result_count = 3U,
      .error_category = "none",
  };
}

void test_knowledge_telemetry_emits_all_event_families_to_available_sinks() {
  std::vector<KnowledgeTelemetryEvent> log_events;
  std::vector<KnowledgeTelemetryEvent> metrics_events;
  std::vector<KnowledgeTelemetryEvent> trace_events;
  std::vector<KnowledgeTelemetryEvent> audit_events;

  KnowledgeTelemetry telemetry(TelemetrySinks{
      .log_sink = [&log_events](const KnowledgeTelemetryEvent& event) { log_events.push_back(event); },
      .metrics_sink = [&metrics_events](const KnowledgeTelemetryEvent& event) {
        metrics_events.push_back(event);
      },
      .trace_sink = [&trace_events](const KnowledgeTelemetryEvent& event) { trace_events.push_back(event); },
      .audit_sink = [&audit_events](const KnowledgeTelemetryEvent& event) { audit_events.push_back(event); },
      .fallback_log_sink = {},
  });

  telemetry.emit_retrieve_event(make_retrieve_event("knowledge_query_completed"));

  auto ingest_event = make_retrieve_event("knowledge_ingest_applied");
  ingest_event.request_id.clear();
  ingest_event.query_kind.reset();
  ingest_event.retrieval_mode.reset();
  telemetry.emit_ingest_event(ingest_event);

  auto health_event = make_retrieve_event("knowledge_stale_snapshot_served");
  health_event.request_id.clear();
  health_event.query_kind.reset();
  health_event.retrieval_mode.reset();
  telemetry.emit_health_event(health_event);

  auto swap_event = make_retrieve_event("knowledge_snapshot_swap_completed");
  swap_event.request_id.clear();
  swap_event.query_kind.reset();
  swap_event.retrieval_mode.reset();
  telemetry.emit_snapshot_swap_event(swap_event);

  const auto status = telemetry.status();
  assert_true(status.has_consistent_values(),
              "telemetry status should remain internally consistent after emitting all event families");
  assert_equal(4, static_cast<int>(log_events.size()),
               "log sink should receive retrieve, ingest, health, and snapshot swap events");
  assert_equal(4, static_cast<int>(metrics_events.size()),
               "metrics sink should receive retrieve, ingest, health, and snapshot swap events");
  assert_equal(4, static_cast<int>(trace_events.size()),
               "trace sink should receive retrieve, ingest, health, and snapshot swap events");
  assert_equal(4, static_cast<int>(audit_events.size()),
               "audit sink should receive retrieve, ingest, health, and snapshot swap events");
  assert_equal(1, static_cast<int>(status.retrieve_event_total),
               "retrieve event counter should increment once");
  assert_equal(1, static_cast<int>(status.ingest_event_total),
               "ingest event counter should increment once");
  assert_equal(1, static_cast<int>(status.health_event_total),
               "health event counter should increment once");
  assert_equal(1, static_cast<int>(status.snapshot_swap_event_total),
               "snapshot swap event counter should increment once");
  assert_equal(0, static_cast<int>(status.sink_failure_total),
               "successful sinks should not register sink failures");
}

}  // namespace

int main() {
  try {
    test_knowledge_telemetry_emits_all_event_families_to_available_sinks();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}