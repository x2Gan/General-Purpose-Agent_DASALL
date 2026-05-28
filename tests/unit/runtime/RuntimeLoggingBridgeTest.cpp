#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>

#include "budget/BudgetDecision.h"
#include "checkpoint/RecoveryOutcome.h"
#include "logging/LoggingFacade.h"
#include "support/TestAssertions.h"
#include "telemetry/RuntimeLoggingBridge.h"
#include "telemetry/RuntimeTelemetryBridge.h"

namespace {

using dasall::contracts::RecoveryOutcome;
using dasall::infra::InfraContext;
using dasall::infra::logging::ILogDispatchBackend;
using dasall::infra::logging::LogEvent;
using dasall::infra::logging::LogFlushDeadline;
using dasall::infra::logging::LogWriteResult;
using dasall::infra::logging::LoggingFacade;
using dasall::runtime::RuntimeLoggingBridge;
using dasall::runtime::RuntimeState;
using dasall::runtime::RuntimeTelemetryBridge;
using dasall::runtime::RuntimeTelemetryBridgeOptions;
using dasall::runtime::RuntimeTelemetryContext;
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

[[nodiscard]] RuntimeTelemetryContext make_context() {
  return RuntimeTelemetryContext{
      .request_id = std::string("req-runtime-logging-bridge"),
      .session_id = std::string("session-runtime-logging-bridge"),
      .trace_id = std::string("trace-runtime-logging-bridge"),
      .turn_id = std::string("turn-runtime-logging-bridge"),
      .checkpoint_id = std::string("chk-runtime-logging-bridge"),
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

[[nodiscard]] std::shared_ptr<LoggingFacade> make_logger(RecordingDispatchBackend** backend_out) {
  auto backend = std::make_unique<RecordingDispatchBackend>();
  *backend_out = backend.get();
  auto logger = std::make_shared<LoggingFacade>(std::move(backend));
  assert_true(logger->init(InfraContext{
                          .request_id = std::string("req-runtime-logging-test"),
                          .session_id = std::string("session-runtime-logging-test"),
                          .trace_id = std::string("trace-runtime-logging-test"),
                          .task_id = std::string("task-runtime-logging-test"),
                          .parent_task_id = std::string("parent-runtime-logging-test"),
                          .lease_id = std::string("lease-runtime-logging-test"),
                      })
                      .ok,
              "runtime logging bridge unit test should initialize the logger before dispatch");
  return logger;
}

void test_runtime_logging_bridge_projects_transition_event_to_allowlisted_attrs() {
  RecordingDispatchBackend* backend = nullptr;
  const auto logger = make_logger(&backend);
  RuntimeLoggingBridge bridge(logger);
  RuntimeTelemetryBridge telemetry_bridge(
      nullptr,
      RuntimeTelemetryBridgeOptions{
          .runtime_instance_id = "runtime-bridge-unit",
          .now_ms = []() { return 1700000010001LL; },
      });

  auto envelope = telemetry_bridge.emit_transition(
                                RuntimeState::Planning,
                                RuntimeState::Reasoning,
                                make_context(),
                                "transition_secret=hidden payload_json=raw")
                      .envelope;
  envelope.attributes.push_back({.key = "payload_json", .value = "raw-secret-payload"});
  envelope.attributes.push_back({.key = "checkpoint_ref", .value = "checkpoint-secret"});

  const auto result = bridge.handle(envelope);
  assert_true(result.ok,
              "runtime logging bridge should accept transition envelopes when a logger is available");
  assert_equal(1,
               backend->dispatch_count(),
               "runtime logging bridge should dispatch exactly one projected transition record");
  assert_true(logger->has_last_dispatched_event(),
              "runtime logging bridge should leave a last dispatched event on the logger");

  const auto& dispatched = logger->last_dispatched_event();
  assert_true(has_attr(dispatched.attrs, "event_name", "runtime.transition") &&
                  has_attr(dispatched.attrs, "category", "transition") &&
                  has_attr(dispatched.attrs, "severity", "info") &&
                  has_attr(dispatched.attrs, "request_id", "req-runtime-logging-bridge") &&
                  has_attr(dispatched.attrs, "session_id", "session-runtime-logging-bridge") &&
                  has_attr(dispatched.attrs, "trace_id", "trace-runtime-logging-bridge") &&
                  has_attr(dispatched.attrs, "turn_id", "turn-runtime-logging-bridge") &&
                  has_attr(dispatched.attrs, "checkpoint_id", "chk-runtime-logging-bridge") &&
                  has_attr(dispatched.attrs, "from_state", "Planning") &&
                  has_attr(dispatched.attrs, "to_state", "Reasoning") &&
                  has_attr(dispatched.attrs, "runtime_instance_id", "runtime-bridge-unit"),
              "runtime logging bridge should project transition event context and allowlisted runtime attrs into the logger");
  assert_true(!has_key(dispatched.attrs, "payload_json") &&
                  !has_key(dispatched.attrs, "checkpoint_ref"),
              "runtime logging bridge should drop non-allowlisted runtime attrs from ordinary log records");
  assert_true(dispatched.message.find("transition_secret") == std::string::npos &&
                  dispatched.message.find("raw-secret-payload") == std::string::npos,
              "runtime logging bridge should not copy event detail or forbidden payloads into the dispatched log message");
}

void test_runtime_logging_bridge_marks_audit_events_as_pending_without_checkpoint_payload() {
  RecordingDispatchBackend* backend = nullptr;
  const auto logger = make_logger(&backend);
  RuntimeLoggingBridge bridge(logger);
  RuntimeTelemetryBridge telemetry_bridge(
      nullptr,
      RuntimeTelemetryBridgeOptions{
          .runtime_instance_id = "runtime-bridge-audit",
          .now_ms = []() { return 1700000011001LL; },
      });

  const auto envelope = telemetry_bridge.emit_recovery_reject(
                            RecoveryOutcome{
                                .executed_action = std::string("abort_safe"),
                                .final_runtime_state = std::string("FailedSafe"),
                                .updated_retry_count = 2U,
                                .checkpoint_ref = std::string("checkpoint-secret"),
                                .compensation_result_ref = std::nullopt,
                                .rejection_reason = std::string("retry budget exhausted"),
                                .escalation_reason = std::nullopt,
                            },
                            make_context(),
                            "recovery_secret=raw-detail")
                            .envelope;

  const auto result = bridge.handle(envelope);
  assert_true(result.ok,
              "runtime logging bridge should accept audit-marked runtime envelopes for redacted operational logging");
  assert_equal(1,
               backend->dispatch_count(),
               "runtime logging bridge should dispatch the projected audit-marked record exactly once");

  const auto& dispatched = logger->last_dispatched_event();
  assert_true(has_attr(dispatched.attrs, "event_name", "runtime.recovery.reject") &&
                  has_attr(dispatched.attrs, "category", "recovery_reject") &&
                  has_attr(dispatched.attrs, "severity", "error") &&
                  has_attr(dispatched.attrs, "audit_ref_pending", "true") &&
                  has_attr(dispatched.attrs, "executed_action", "abort_safe") &&
                  has_attr(dispatched.attrs, "final_runtime_state", "FailedSafe") &&
                  has_attr(dispatched.attrs, "error_code", "500"),
              "runtime logging bridge should retain only redacted operational attrs for audit-marked runtime envelopes");
  assert_true(!has_key(dispatched.attrs, "checkpoint_ref"),
              "runtime logging bridge should not project checkpoint_ref into ordinary runtime log attrs");
  assert_true(dispatched.message.find("recovery_secret") == std::string::npos &&
                  dispatched.message.find("checkpoint-secret") == std::string::npos,
              "runtime logging bridge should not leak audit detail or checkpoint payload into the dispatched log message");
}

}  // namespace

int main() {
  try {
    test_runtime_logging_bridge_projects_transition_event_to_allowlisted_attrs();
    test_runtime_logging_bridge_marks_audit_events_as_pending_without_checkpoint_payload();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}