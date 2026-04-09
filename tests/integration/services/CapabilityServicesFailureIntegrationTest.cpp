#include <algorithm>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "CapabilityServicesLoopbackFixture.h"
#include "audit/IAuditLogger.h"
#include "bridges/ServiceAuditBridge.h"
#include "bridges/ServiceMetricsBridge.h"
#include "metrics/MetricTypes.h"
#include "support/TestAssertions.h"

namespace {

using dasall::services::internal::AdapterInvocationRequest;
using dasall::services::internal::AdapterInvocationResult;
using dasall::services::internal::AdapterTransportOutcome;
using dasall::services::internal::ServiceAuditBridge;
using dasall::services::internal::ServiceMetricsBridge;
using dasall::services::internal::ServiceMetricsBridgeOptions;
using dasall::tests::mocks::CapabilityServicesLoopbackFixture;
using dasall::tests::mocks::CapabilityServicesLoopbackFixtureOptions;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

class RecordingAuditLogger final : public dasall::infra::audit::IAuditLogger {
 public:
  dasall::infra::AuditWriteOutcome write_audit(
      const dasall::infra::AuditEvent& event,
      const dasall::infra::AuditContext& context) override {
    events.push_back(event);
    contexts.push_back(context);
    return dasall::infra::AuditWriteOutcome{
        .accepted = true,
        .persisted = true,
        .fallback_used = false,
        .error_code = std::nullopt,
    };
  }

  dasall::infra::ExportResult export_audit(
      const dasall::infra::ExportQuery&) override {
    return dasall::infra::ExportResult{};
  }

  std::vector<dasall::infra::AuditEvent> events;
  std::vector<dasall::infra::AuditContext> contexts;
};

class RecordingMeter final : public dasall::infra::metrics::IMeter {
 public:
  std::optional<dasall::infra::metrics::InstrumentHandle> create_counter(
      const dasall::infra::metrics::MetricIdentity& identity) override {
    created_identities.push_back(identity);
    return dasall::infra::metrics::InstrumentHandle{
        .instrument_key = identity.name + ":counter",
    };
  }

  std::optional<dasall::infra::metrics::InstrumentHandle> create_gauge(
      const dasall::infra::metrics::MetricIdentity& identity) override {
    created_identities.push_back(identity);
    return dasall::infra::metrics::InstrumentHandle{
        .instrument_key = identity.name + ":gauge",
    };
  }

  std::optional<dasall::infra::metrics::InstrumentHandle> create_histogram(
      const dasall::infra::metrics::MetricIdentity& identity) override {
    created_identities.push_back(identity);
    return dasall::infra::metrics::InstrumentHandle{
        .instrument_key = identity.name + ":histogram",
    };
  }

  dasall::infra::metrics::MetricsOperationStatus record(
      const dasall::infra::metrics::MetricSample& sample) override {
    recorded_samples.push_back(sample);
    return dasall::infra::metrics::MetricsOperationStatus::success(
        "metrics://services/failure-recorded");
  }

  std::vector<dasall::infra::metrics::MetricIdentity> created_identities;
  std::vector<dasall::infra::metrics::MetricSample> recorded_samples;
};

class RecordingMetricsProvider final
    : public dasall::infra::metrics::IMetricsProvider {
 public:
  explicit RecordingMetricsProvider(std::shared_ptr<RecordingMeter> meter)
      : meter_(std::move(meter)) {}

  dasall::infra::metrics::MetricsOperationStatus init(
      const dasall::infra::metrics::MetricsProviderConfig&) override {
    return dasall::infra::metrics::MetricsOperationStatus::success(
        "metrics://services/failure-provider-init");
  }

  std::shared_ptr<dasall::infra::metrics::IMeter> get_meter(
      const dasall::infra::metrics::MeterScope& scope) override {
    last_scope = scope;
    return meter_;
  }

  dasall::infra::metrics::MetricsOperationStatus force_flush(
      const dasall::infra::metrics::MetricsCallDeadline&) override {
    return dasall::infra::metrics::MetricsOperationStatus::success(
        "metrics://services/failure-provider-flush");
  }

  dasall::infra::metrics::MetricsOperationStatus shutdown(
      const dasall::infra::metrics::MetricsCallDeadline&) override {
    return dasall::infra::metrics::MetricsOperationStatus::success(
        "metrics://services/failure-provider-shutdown");
  }

  dasall::infra::metrics::MeterScope last_scope{};

 private:
  std::shared_ptr<RecordingMeter> meter_;
};

struct RecordingMetricsHarness {
  RecordingMetricsHarness()
      : meter(std::make_shared<RecordingMeter>()),
        provider(std::make_shared<RecordingMetricsProvider>(meter)),
        bridge(provider,
               ServiceMetricsBridgeOptions{
                   .enabled = true,
                   .profile_id = "desktop_full",
                   .metrics_granularity = "full",
                   .meter_scope_name = "services",
                   .meter_scope_version = "v1",
                   .now_ms = []() { return 1712746905000LL; },
               }) {}

  std::shared_ptr<RecordingMeter> meter;
  std::shared_ptr<RecordingMetricsProvider> provider;
  ServiceMetricsBridge bridge;
};

[[nodiscard]] bool contains_string(const std::vector<std::string>& values,
                                   const std::string& expected) {
  return std::find(values.begin(), values.end(), expected) != values.end();
}

[[nodiscard]] bool has_action(
    const std::vector<dasall::infra::AuditEvent>& events,
    const std::string& action) {
  return std::any_of(events.begin(), events.end(), [&](const auto& event) {
    return event.action == action;
  });
}

[[nodiscard]] bool has_sample(
    const std::vector<dasall::infra::metrics::MetricSample>& samples,
    const std::string& name,
    const std::string& stage,
    const std::string& outcome,
    const std::string& error_code) {
  return std::any_of(samples.begin(), samples.end(), [&](const auto& sample) {
    return sample.identity_ref.name == name && sample.labels.stage == stage &&
           sample.labels.outcome == outcome &&
           sample.labels.error_code == error_code;
  });
}

void test_capability_services_failure_integration_maps_remote_timeout_to_provider_failure() {
  RecordingMetricsHarness metrics;

  CapabilityServicesLoopbackFixtureOptions options;
  options.local_service_available = false;
  options.remote_service_available = true;
  options.remote_timeout = true;
  options.metrics_bridge = &metrics.bridge;

  CapabilityServicesLoopbackFixture fixture(std::move(options));
  const auto result = fixture.execution_service().execute(
      fixture.make_execute_request("req-failure-timeout",
                                   "target-failure-timeout",
                                   "toggle",
                                   "{\"state\":\"off\"}"));

  assert_true(result.error.has_value(),
              "remote timeout should surface a structured provider failure result");
  assert_true(result.error->failure_type.has_value() &&
                  *result.error->failure_type ==
                      dasall::contracts::ResultCodeCategory::Provider &&
            result.error->retryable.has_value() &&
            *result.error->retryable &&
            result.error->safe_to_replan.has_value() &&
            *result.error->safe_to_replan,
              "remote timeout should remain retryable and safe_to_replan according to adapter_unavailable semantics");
  assert_equal(std::string("adapter_receipt"),
               result.error->source_ref.ref_type,
               "remote timeout should preserve adapter_receipt source refs");
  assert_true(result.error->source_ref.ref_id.find("loopback.remote_service") !=
                  std::string::npos,
              "remote timeout should anchor the failure on the remote adapter receipt even when the adapter short-circuits before the loopback callback");
  assert_equal(0,
               static_cast<int>(fixture.local_requests().size()),
               "remote timeout should bypass the unavailable local route");
  assert_equal(0,
               static_cast<int>(fixture.remote_requests().size()),
               "remote timeout should not enter the loopback remote callback when the adapter times out before invoking it");
  assert_true(has_sample(metrics.meter->recorded_samples,
                         "services_execution_requests_total",
                         "execution.command.toggle",
                         "degraded",
                         "ProviderTimeout") &&
                  has_sample(metrics.meter->recorded_samples,
                             "services_execution_latency_ms",
                             "execution.adapter.loopback.remote_service.toggle",
                             "degraded",
                             "ProviderTimeout"),
              "remote timeout should emit degraded execution request and adapter latency metrics");
}

void test_capability_services_failure_integration_preserves_partial_side_effect_audit_and_metrics() {
  RecordingAuditLogger audit_logger;
  ServiceAuditBridge audit_bridge(&audit_logger);
  RecordingMetricsHarness metrics;

  CapabilityServicesLoopbackFixtureOptions options;
  options.high_risk_actions = {"toggle"};
  options.audit_bridge = &audit_bridge;
  options.metrics_bridge = &metrics.bridge;
  options.lookup_compensation_hints = [](const std::string&,
                                         const std::string& action,
                                         const std::string&,
                                         const dasall::services::internal::AdapterReceipt&) {
    return std::vector<std::string>{action + ".rollback"};
  };
  options.local_handler = [](const AdapterInvocationRequest& request) {
    return AdapterInvocationResult{
        .transport_outcome = AdapterTransportOutcome::partial,
        .provider_status_code = "partial_side_effect",
        .payload_json = request.payload_json,
        .latency_ms = 9U,
        .side_effects = {request.operation_name + ".applied"},
        .evidence_refs = {std::string("evidence://services/") + request.operation_name},
    };
  };

  CapabilityServicesLoopbackFixture fixture(std::move(options));
  auto request = fixture.make_execute_request("req-failure-partial",
                                              "target-failure-partial",
                                              "toggle",
                                              "{\"state\":\"on\"}");
  request.idempotency_key = std::string("idem-failure-partial");
  const auto result = fixture.execution_service().execute(request);

  assert_true(result.error.has_value(),
              "partial side effect should surface a structured degraded execution result");
  assert_true(result.error->failure_type.has_value() &&
                  *result.error->failure_type ==
                      dasall::contracts::ResultCodeCategory::Provider &&
            result.error->retryable.has_value() &&
            !*result.error->retryable &&
            result.error->safe_to_replan.has_value() &&
            !*result.error->safe_to_replan,
              "partial side effect should preserve provider failure semantics without retry or replanning");
  assert_equal(std::string("toggle.applied"),
               result.side_effects.front(),
               "partial side effect should preserve the emitted side effect fact");
  assert_equal(std::string("toggle.rollback"),
               result.compensation_hints.front(),
               "partial side effect should preserve the compensation hint for later recovery");
  assert_equal(std::string("evidence_ref"),
               result.error->source_ref.ref_type,
               "partial side effect should anchor the error on the evidence ref");
  assert_equal(std::string("evidence://services/toggle"),
               result.error->source_ref.ref_id,
               "partial side effect should surface the first evidence ref as source_ref");
  assert_equal(2,
               static_cast<int>(audit_logger.events.size()),
               "high-risk partial side effect should emit requested and completed audit events");
  assert_true(has_action(audit_logger.events, "service.execution.requested") &&
                  has_action(audit_logger.events, "service.execution.completed"),
              "partial side effect should preserve the frozen execution audit event family");
  assert_true(contains_string(audit_logger.events.front().side_effects,
                              "request_id:req-failure-partial") &&
                  contains_string(audit_logger.events.back().side_effects,
                                  "toggle.applied") &&
                  contains_string(audit_logger.events.back().side_effects,
                                  "compensation_hint:toggle.rollback") &&
                  contains_string(audit_logger.events.back().side_effects,
                                  "result_code:ProviderTimeout"),
              "partial side effect audit evidence should retain request, side effect, compensation hint, and result code facts");
  assert_equal(2,
               static_cast<int>(audit_bridge.get_status().emitted_total),
               "partial side effect should remain visible in the audit bridge status snapshot");
  assert_true(has_sample(metrics.meter->recorded_samples,
                         "services_execution_requests_total",
                         "execution.command.toggle",
                         "degraded",
                         "ProviderTimeout") &&
                  has_sample(metrics.meter->recorded_samples,
                             "services_execution_latency_ms",
                             "execution.adapter.loopback.local_service.toggle",
                             "degraded",
                             "ProviderTimeout") &&
                  has_sample(metrics.meter->recorded_samples,
                             "services_compensation_hint_total",
                             "execution.compensation.toggle",
                             "degraded",
                             "ProviderTimeout"),
              "partial side effect should emit degraded request, adapter latency, and compensation hint metrics");
}

void test_capability_services_failure_integration_reports_subscription_overflow_and_metrics() {
  RecordingMetricsHarness metrics;

  CapabilityServicesLoopbackFixtureOptions options;
  options.metrics_bridge = &metrics.bridge;
  options.max_buffered_subscription_events = 1U;

  CapabilityServicesLoopbackFixture fixture(std::move(options));
  fixture.publish_subscription_events("target-failure-subscription",
                                      "status",
                                      {"{\"seq\":1}", "{\"seq\":2}", "{\"seq\":3}"});
  const auto result = fixture.execution_service().subscribe(
      fixture.make_subscription_request("req-failure-subscription",
                                        "target-failure-subscription",
                                        "status",
                                        std::string("0"),
                                        2U));

  assert_true(result.error.has_value() && result.resync_required &&
                  result.dropped_count == 2U,
              "subscription overflow should preserve resync_required and dropped_count through the public execution service");
  assert_true(result.error->failure_type.has_value() &&
                  *result.error->failure_type ==
                      dasall::contracts::ResultCodeCategory::Runtime &&
            result.error->retryable.has_value() &&
            *result.error->retryable &&
            result.error->safe_to_replan.has_value() &&
            !*result.error->safe_to_replan,
              "subscription overflow should remain a runtime retryable failure that still blocks replanning");
  assert_equal(std::string("subscription_stream"),
               result.error->source_ref.ref_type,
               "subscription overflow should anchor errors on the subscription stream ref");
  assert_true(result.next_cursor.has_value() && *result.next_cursor == "3" &&
                  result.events_json == "[{\"seq\":3}]",
              "subscription overflow should still return the latest buffered event and cursor while demanding resync");
  assert_true(has_sample(metrics.meter->recorded_samples,
                         "services_subscription_overflow_total",
                         "subscription.status.cap.exec",
                         "degraded",
                         "RuntimeRetryExhausted"),
              "subscription overflow should emit the frozen subscription overflow metric family");
}

void test_capability_services_failure_integration_surfaces_circuit_open_route_and_metrics() {
  RecordingMetricsHarness metrics;

  CapabilityServicesLoopbackFixtureOptions options;
  options.local_service_available = false;
  options.metrics_bridge = &metrics.bridge;

  CapabilityServicesLoopbackFixture fixture(std::move(options));
  const auto result = fixture.execution_service().execute(
      fixture.make_execute_request("req-failure-circuit",
                                   "target-failure-circuit",
                                   "toggle",
                                   "{\"state\":\"off\"}"));

  assert_true(result.error.has_value(),
              "route-unavailable execution should surface a structured circuit-open style failure");
  assert_true(result.error->failure_type.has_value() &&
                  *result.error->failure_type ==
                      dasall::contracts::ResultCodeCategory::Runtime &&
            result.error->retryable.has_value() &&
            *result.error->retryable &&
            result.error->safe_to_replan.has_value() &&
            *result.error->safe_to_replan,
              "route-unavailable execution should preserve runtime retryable circuit-open semantics");
  assert_equal(std::string("route_receipt"),
               result.error->source_ref.ref_type,
               "route-unavailable execution should preserve route_receipt source refs");
  assert_equal(0,
               static_cast<int>(fixture.local_requests().size()),
               "circuit-open routing should fail before any local adapter invocation runs");
  assert_equal(0,
               static_cast<int>(fixture.remote_requests().size()),
               "circuit-open routing should fail before any remote adapter invocation runs");
  assert_true(has_sample(metrics.meter->recorded_samples,
                         "services_execution_circuit_open_total",
                         "execution.circuit.toggle.unknown",
                         "degraded",
                         "supported_routes_exist_but_no_candidate_is_currently_selectable") &&
                  has_sample(metrics.meter->recorded_samples,
                             "services_execution_requests_total",
                             "execution.command.toggle",
                             "degraded",
                             "RuntimeRetryExhausted"),
              "circuit-open routing should emit both the dedicated circuit metric and the degraded execution request metric");
}

}  // namespace

int main() {
  try {
    test_capability_services_failure_integration_maps_remote_timeout_to_provider_failure();
    test_capability_services_failure_integration_preserves_partial_side_effect_audit_and_metrics();
    test_capability_services_failure_integration_reports_subscription_overflow_and_metrics();
    test_capability_services_failure_integration_surfaces_circuit_open_route_and_metrics();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}