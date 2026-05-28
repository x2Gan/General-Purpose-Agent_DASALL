#include <exception>
#include <iostream>
#include <stdexcept>
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

[[nodiscard]] KnowledgeTelemetryEvent make_degraded_event() {
  return KnowledgeTelemetryEvent{
      .event_name = "knowledge_query_degraded",
      .request_id = "req-knowledge-degrade",
      .session_id = std::string(),
      .component = "RecallCoordinator",
      .snapshot_id = "snapshot-knowledge-2",
      .result = "degraded",
      .degraded = true,
      .latency_ms = 73,
      .reason_codes = {"vector_backend_unavailable"},
      .corpus_ids = {"adr_normative"},
      .profile_id = "edge_balanced",
      .query_kind = KnowledgeQueryKind::DiagnosticContext,
      .retrieval_mode = RetrievalMode::Hybrid,
      .warning_summary = {},
      .corpus_count = 1U,
      .result_count = 2U,
      .error_category = "provider",
  };
}

void test_knowledge_telemetry_sink_failures_are_fail_open_and_record_drop_facts() {
  std::vector<KnowledgeTelemetryEvent> log_events;
  std::vector<KnowledgeTelemetryEvent> audit_events;
  std::vector<KnowledgeTelemetryEvent> fallback_events;

  KnowledgeTelemetry telemetry(TelemetrySinks{
      .log_sink = [&log_events](const KnowledgeTelemetryEvent& event) { log_events.push_back(event); },
      .metrics_sink = [](const KnowledgeTelemetryEvent&) {
        throw std::runtime_error("metrics exporter unavailable");
      },
      .trace_sink = {},
      .audit_sink = [&audit_events](const KnowledgeTelemetryEvent& event) {
        audit_events.push_back(event);
      },
      .fallback_log_sink = [&fallback_events](const KnowledgeTelemetryEvent& event) {
        fallback_events.push_back(event);
      },
  });

  telemetry.emit_retrieve_event(make_degraded_event());

  const auto status = telemetry.status();
  assert_equal(1, static_cast<int>(log_events.size()),
               "successful sinks should still receive degraded retrieve events even when another sink fails");
  assert_equal(1, static_cast<int>(audit_events.size()),
               "audit sink should still receive degraded retrieve events when metrics fails");
  assert_equal(1, static_cast<int>(fallback_events.size()),
               "sink failure should trigger exactly one fallback log emission");
  assert_true(fallback_events.front().degraded,
              "fallback log should preserve the degraded flag when a sink write fails");
  assert_true(std::find(fallback_events.front().reason_codes.begin(),
                        fallback_events.front().reason_codes.end(),
                        std::string("metrics_sink_failure")) !=
                  fallback_events.front().reason_codes.end(),
              "fallback log should preserve the sink failure reason code for later diagnosis");
  assert_true(status.degraded,
              "telemetry status should enter degraded mode when any sink write fails");
  assert_equal(1, static_cast<int>(status.sink_failure_total),
               "sink failure counter should increase exactly once for the failing metrics sink");
  assert_equal(1, static_cast<int>(status.dropped_event_total),
               "sink failure should count as one dropped delivery fact");
  assert_equal(1, static_cast<int>(status.fallback_log_total),
               "fallback log counter should track the preserved degraded event");
}

}  // namespace

int main() {
  try {
    test_knowledge_telemetry_sink_failures_are_fail_open_and_record_drop_facts();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}