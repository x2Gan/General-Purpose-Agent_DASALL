#pragma once

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "observability/CognitionTelemetry.h"

namespace dasall::tests::mocks {

struct MockCognitionTelemetrySinkConfig {
  bool fail_log = false;
  bool fail_metric = false;
  bool fail_trace = false;
  bool fail_audit = false;
};

class MockCognitionTelemetrySink final
    : public dasall::cognition::observability::ICognitionTelemetrySink {
 public:
  explicit MockCognitionTelemetrySink(MockCognitionTelemetrySinkConfig config = {})
      : config_(std::move(config)) {}

  void emit_log(const dasall::cognition::observability::TelemetryEvent& event) override {
    if (config_.fail_log) {
      throw std::runtime_error("mock telemetry log sink failure");
    }
    log_events.push_back(event);
  }

  void emit_metric(
      const dasall::cognition::observability::TelemetryMetric& metric) override {
    if (config_.fail_metric) {
      throw std::runtime_error("mock telemetry metric sink failure");
    }
    metrics.push_back(metric);
  }

  void emit_trace(const dasall::cognition::observability::TelemetryEvent& event) override {
    if (config_.fail_trace) {
      throw std::runtime_error("mock telemetry trace sink failure");
    }
    trace_events.push_back(event);
  }

  void emit_audit(const dasall::cognition::observability::TelemetryEvent& event) override {
    if (config_.fail_audit) {
      throw std::runtime_error("mock telemetry audit sink failure");
    }
    audit_events.push_back(event);
  }

  MockCognitionTelemetrySinkConfig config_;
  std::vector<dasall::cognition::observability::TelemetryEvent> log_events;
  std::vector<dasall::cognition::observability::TelemetryMetric> metrics;
  std::vector<dasall::cognition::observability::TelemetryEvent> trace_events;
  std::vector<dasall::cognition::observability::TelemetryEvent> audit_events;
};

}  // namespace dasall::tests::mocks