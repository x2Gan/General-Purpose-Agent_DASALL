#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>

#include "logging/LoggingFacade.h"
#include "support/TestAssertions.h"

#include "bridges/ServiceLoggingBridge.h"

namespace {

using dasall::infra::InfraContext;
using dasall::infra::logging::ILogDispatchBackend;
using dasall::infra::logging::LogEvent;
using dasall::infra::logging::LogFlushDeadline;
using dasall::infra::logging::LogWriteResult;
using dasall::infra::logging::LoggingFacade;
using dasall::services::CapabilityTargetRef;
using dasall::services::ServiceCallContext;
using dasall::services::internal::AdapterAvailabilityState;
using dasall::services::internal::AdapterReceipt;
using dasall::services::internal::AdapterRouteKind;
using dasall::services::internal::AdapterSelection;
using dasall::services::internal::AdapterTransportOutcome;
using dasall::services::internal::AdapterTrustClass;
using dasall::services::internal::ServiceLoggingBridge;
using dasall::services::internal::ServiceLoggingBridgeOptions;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

class RecordingDispatchBackend final : public ILogDispatchBackend {
 public:
  LogWriteResult dispatch(const LogEvent& event) override {
    last_event_ = event;
    ++dispatch_count_;
    return LogWriteResult::success();
  }

  LogWriteResult flush(const LogFlushDeadline&) override {
    return LogWriteResult::success();
  }

  [[nodiscard]] int dispatch_count() const {
    return dispatch_count_;
  }

 private:
  int dispatch_count_ = 0;
  std::optional<LogEvent> last_event_;
};

[[nodiscard]] std::shared_ptr<LoggingFacade> make_logger(RecordingDispatchBackend** backend_out) {
  auto backend = std::make_unique<RecordingDispatchBackend>();
  *backend_out = backend.get();
  auto logger = std::make_shared<LoggingFacade>(std::move(backend));
  assert_true(logger->init(InfraContext{
                          .request_id = std::string("req-services-logging-test"),
                          .session_id = std::string("session-services-logging-test"),
                          .trace_id = std::string("trace-services-logging-test"),
                          .task_id = std::string("task-services-logging-test"),
                          .parent_task_id = std::string("parent-services-logging-test"),
                          .lease_id = std::string("lease-services-logging-test"),
                      })
                      .ok,
              "service logging bridge unit test should initialize the logger before dispatch");
  return logger;
}

[[nodiscard]] ServiceCallContext make_context() {
  dasall::contracts::RuntimeBudget budget;
  budget.max_latency_ms = 4000U;
  return ServiceCallContext{
      .request_id = "req-services-logging-bridge",
      .session_id = "session-services-logging-bridge",
      .trace_id = "trace-services-logging-bridge",
      .tool_call_id = "tool-call-services-logging-bridge",
      .goal_id = "goal-services-logging-bridge",
      .budget_guard = budget,
      .deadline_ms = 12000U,
  };
}

[[nodiscard]] bool has_attr(const LogEvent::AttributeMap& attrs,
                            const std::string& key,
                            const std::string& value) {
  const auto it = attrs.find(key);
  return it != attrs.end() && it->second == value;
}

[[nodiscard]] bool has_key(const LogEvent::AttributeMap& attrs,
                           const std::string& key) {
  return attrs.find(key) != attrs.end();
}

void test_service_logging_bridge_projects_allowlisted_route_attrs() {
  RecordingDispatchBackend* backend = nullptr;
  const auto logger = make_logger(&backend);
  ServiceLoggingBridge bridge(
      logger,
      ServiceLoggingBridgeOptions{
          .enabled = true,
          .now_ms = []() { return 1700000020001LL; },
      });

  const auto target = CapabilityTargetRef{
      .capability_id = "cap.exec",
      .target_id = "target-services-logging",
  };
  const auto selection = AdapterSelection{
      .route_kind = AdapterRouteKind::local_service,
      .adapter_id = "loopback.local_service",
      .target_id = "target-services-logging",
      .route_equivalence_class = "service.live",
      .fallback_hop = 0U,
      .selected_reason = "preferred_locality",
      .trust_class = AdapterTrustClass::caller_verified,
      .availability_state = AdapterAvailabilityState::available,
  };
  const auto receipt = AdapterReceipt{
      .receipt_ref = "receipt-services-logging",
      .adapter_id = "loopback.local_service",
      .route_kind = AdapterRouteKind::local_service,
      .target_id = "target-services-logging",
      .transport_outcome = AdapterTransportOutcome::acknowledged,
      .provider_status_code = "ok",
      .payload_json = "{\"secret\":\"raw\"}",
      .latency_ms = 25U,
      .side_effects = {"effect-a", "effect-b"},
      .evidence_refs = {"evidence-1"},
  };

  const auto result = bridge.write_execution_route(
      make_context(), target, "toggle", selection, receipt);
  assert_true(result.ok,
              "service logging bridge should accept execution route events when a logger is available");
  assert_equal(1,
               backend->dispatch_count(),
               "service logging bridge should dispatch exactly one projected services log record");
  assert_true(logger->has_last_dispatched_event(),
              "service logging bridge should leave a last dispatched event on the logger");

  const auto& dispatched = logger->last_dispatched_event();
  assert_true(has_attr(dispatched.attrs, "event_name", "service.execution.route") &&
                  has_attr(dispatched.attrs, "request_id",
                           "req-services-logging-bridge") &&
                  has_attr(dispatched.attrs, "capability_id", "cap.exec") &&
                  has_attr(dispatched.attrs, "target_id", "target-services-logging") &&
                  has_attr(dispatched.attrs, "request_kind", "action") &&
                  has_attr(dispatched.attrs, "operation_name", "toggle") &&
                  has_attr(dispatched.attrs, "route_kind", "local_service") &&
                  has_attr(dispatched.attrs, "adapter_id", "loopback.local_service") &&
                  has_attr(dispatched.attrs, "trust_class", "caller_verified") &&
                  has_attr(dispatched.attrs, "availability_state", "available") &&
                  has_attr(dispatched.attrs, "transport_outcome", "acknowledged") &&
                  has_attr(dispatched.attrs, "provider_status_code", "ok") &&
                  has_attr(dispatched.attrs, "latency_ms", "25") &&
                  has_attr(dispatched.attrs, "side_effect_count", "2") &&
                  has_attr(dispatched.attrs, "evidence_ref_count", "1"),
              "service logging bridge should project the frozen services route attrs into the logger");
  assert_true(!has_key(dispatched.attrs, "payload_json") &&
                  !has_key(dispatched.attrs, "side_effects") &&
                  !has_key(dispatched.attrs, "evidence_refs"),
              "service logging bridge should not copy payload or unbounded side-effect detail into ordinary log attrs");
  assert_true(dispatched.module == "services" &&
                  has_attr(dispatched.attrs, "event_name", "service.execution.route"),
              "service logging bridge should keep the frozen services module and event_name after structured formatting");
}

void test_service_logging_bridge_requires_logger_sink() {
  ServiceLoggingBridge bridge;
  const auto result = bridge.write_data_catalog_route(
      make_context(),
      "devices",
      AdapterSelection{
          .route_kind = AdapterRouteKind::local_service,
          .adapter_id = "loopback.local_service",
          .target_id = "devices",
          .route_equivalence_class = "service.live",
          .fallback_hop = 0U,
          .selected_reason = "catalog",
          .trust_class = AdapterTrustClass::caller_verified,
          .availability_state = AdapterAvailabilityState::available,
      },
      AdapterReceipt{
          .receipt_ref = "receipt-services-catalog",
          .adapter_id = "loopback.local_service",
          .route_kind = AdapterRouteKind::local_service,
          .target_id = "devices",
          .transport_outcome = AdapterTransportOutcome::acknowledged,
          .provider_status_code = "ok",
          .payload_json = "{\"catalog\":true}",
          .latency_ms = 12U,
          .side_effects = {},
          .evidence_refs = {},
      });
  assert_true(!result.ok,
              "service logging bridge should surface a local failure when no logger sink is configured");
}

}  // namespace

int main() {
  try {
    test_service_logging_bridge_projects_allowlisted_route_attrs();
    test_service_logging_bridge_requires_logger_sink();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}