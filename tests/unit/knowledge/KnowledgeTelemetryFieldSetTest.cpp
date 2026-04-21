#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "health/KnowledgeTelemetry.h"
#include "support/TestAssertions.h"

namespace {

using dasall::knowledge::KnowledgeTelemetry;
using dasall::knowledge::KnowledgeTelemetryEvent;
using dasall::knowledge::TelemetrySinks;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

void test_knowledge_telemetry_invalid_payloads_are_marked_and_fallback_logged() {
  std::vector<KnowledgeTelemetryEvent> fallback_events;

  KnowledgeTelemetry telemetry(TelemetrySinks{
      .log_sink = {},
      .metrics_sink = {},
      .trace_sink = {},
      .audit_sink = {},
      .fallback_log_sink = [&fallback_events](const KnowledgeTelemetryEvent& event) {
        fallback_events.push_back(event);
      },
  });

  telemetry.emit_retrieve_event(KnowledgeTelemetryEvent{
      .event_name = "knowledge_query_failed",
      .request_id = "",
      .component = "KnowledgeServiceFacade",
      .snapshot_id = "snapshot-knowledge-1",
      .result = "failure",
      .degraded = false,
      .latency_ms = 11,
      .reason_codes = {},
      .corpus_ids = {"adr_normative"},
      .profile_id = "",
      .query_kind = std::nullopt,
      .retrieval_mode = std::nullopt,
      .corpus_count = 1U,
      .result_count = 0U,
      .error_category = "runtime",
  });

  const auto status = telemetry.status();
  assert_equal(1, static_cast<int>(fallback_events.size()),
               "invalid retrieve payload should be redirected to the fallback log path exactly once");
  assert_equal(std::string("invalid_telemetry_payload"), fallback_events.front().result,
               "fallback log should mark the payload result as invalid_telemetry_payload");
  assert_true(fallback_events.front().degraded,
              "fallback log payload should be marked degraded to preserve observability loss evidence");
  assert_true(!fallback_events.front().reason_codes.empty() &&
                  fallback_events.front().reason_codes.front() == "invalid_payload",
              "fallback log payload should carry invalid_payload as the first reason code");
  assert_equal(1, static_cast<int>(status.invalid_payload_total),
               "invalid payload counter should increase when required retrieve fields are missing");
  assert_equal(1, static_cast<int>(status.dropped_event_total),
               "invalid payload should also count as one dropped event");
}

}  // namespace

int main() {
  try {
    test_knowledge_telemetry_invalid_payloads_are_marked_and_fallback_logged();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}