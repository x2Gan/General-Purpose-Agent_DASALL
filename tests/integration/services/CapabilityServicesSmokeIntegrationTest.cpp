#include <algorithm>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "CapabilityServicesLoopbackFixture.h"
#include "audit/IAuditLogger.h"
#include "bridges/ServiceAuditBridge.h"
#include "bridges/ServiceLoggingBridge.h"
#include "logging/LoggingFacade.h"
#include "bridges/ServiceTraceBridge.h"
#include "support/TestAssertions.h"
#include "tracing/ISpan.h"
#include "tracing/ITracer.h"
#include "tracing/ITracerProvider.h"
#include "tracing/TraceTypes.h"

namespace {

using dasall::infra::tracing::ActiveSpanCallback;
using dasall::infra::tracing::ISpan;
using dasall::infra::tracing::ITracer;
using dasall::infra::tracing::ITracerProvider;
using dasall::infra::tracing::SpanDescriptor;
using dasall::infra::tracing::SpanEndResult;
using dasall::infra::tracing::SpanKind;
using dasall::infra::tracing::SpanStatusCode;
using dasall::infra::tracing::TraceAttributeMap;
using dasall::infra::tracing::TraceAttributeValue;
using dasall::infra::tracing::TraceConfig;
using dasall::infra::tracing::TraceContext;
using dasall::infra::tracing::TraceContextState;
using dasall::infra::tracing::TraceOperationStatus;
using dasall::infra::tracing::TracerScope;
using dasall::tests::mocks::CapabilityServicesLoopbackFixture;
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

class RecordingDispatchBackend final : public dasall::infra::logging::ILogDispatchBackend {
 public:
  dasall::infra::logging::LogWriteResult dispatch(
      const dasall::infra::LogEvent& event) override {
    events.push_back(event);
    return dasall::infra::logging::LogWriteResult::success();
  }

  dasall::infra::logging::LogWriteResult flush(
      const dasall::infra::logging::LogFlushDeadline&) override {
    return dasall::infra::logging::LogWriteResult::success();
  }

  std::vector<dasall::infra::LogEvent> events;
};

[[nodiscard]] std::shared_ptr<dasall::infra::logging::LoggingFacade> make_logger(
    RecordingDispatchBackend** backend_out) {
  auto backend = std::make_unique<RecordingDispatchBackend>();
  *backend_out = backend.get();
  auto logger =
      std::make_shared<dasall::infra::logging::LoggingFacade>(std::move(backend));
  assert_true(logger->init(dasall::infra::InfraContext{
                          .request_id = std::string("req-services-smoke-logging"),
                          .session_id = std::string("session-services-smoke-logging"),
                          .trace_id = std::string("trace-services-smoke-logging"),
                          .task_id = std::string("task-services-smoke-logging"),
                          .parent_task_id =
                              std::string("parent-services-smoke-logging"),
                          .lease_id = std::string("lease-services-smoke-logging"),
                      })
                      .ok,
              "services smoke integration should initialize the shared logger before injecting the services logging bridge");
  return logger;
}

[[nodiscard]] bool has_log_attr(const dasall::infra::LogEvent::AttributeMap& attrs,
                                const std::string& key,
                                const std::string& value) {
  const auto it = attrs.find(key);
  return it != attrs.end() && it->second == value;
}

[[nodiscard]] std::string hex_id(std::uint64_t value, std::size_t width) {
  std::ostringstream builder;
  builder << std::hex << std::nouppercase << std::setfill('0')
          << std::setw(static_cast<int>(width)) << value;
  auto encoded = builder.str();
  if (encoded.size() > width) {
    encoded = encoded.substr(encoded.size() - width);
  }
  if (encoded.size() < width) {
    encoded.insert(encoded.begin(), width - encoded.size(), '0');
  }
  if (std::all_of(encoded.begin(), encoded.end(), [](const char ch) {
        return ch == '0';
      })) {
    encoded.back() = '1';
  }
  return encoded;
}

class RecordingSpan final : public ISpan {
 public:
  RecordingSpan(SpanDescriptor descriptor,
                TraceContext context,
                std::optional<TraceContext> parent_context)
      : descriptor(std::move(descriptor)),
        context(std::move(context)),
        parent_context(std::move(parent_context)) {}

  void set_attribute(std::string_view key,
                     const TraceAttributeValue& value) override {
    attributes[std::string(key)] = value;
  }

  void add_event(std::string_view,
                 const TraceAttributeMap&) override {}

  void set_status(SpanStatusCode code,
                  std::string_view message) override {
    status_code = code;
    status_message = std::string(message);
  }

  [[nodiscard]] SpanEndResult end(
      std::optional<std::int64_t> end_ts_unix_ms = std::nullopt) override {
    ++end_call_total;
    if (!end_result.has_value()) {
      end_result = SpanEndResult{
          .end_ts_unix_ms = end_ts_unix_ms.has_value()
                               ? end_ts_unix_ms
                               : std::optional<std::int64_t>(1712746900000LL),
          .status_code = status_code,
          .status_message = status_message,
          .dropped_attr_count = 0U,
      };
    }

    return *end_result;
  }

  [[nodiscard]] TraceContext get_context() const override {
    return context;
  }

  SpanDescriptor descriptor;
  TraceContext context;
  std::optional<TraceContext> parent_context;
  TraceAttributeMap attributes;
  SpanStatusCode status_code = SpanStatusCode::Unset;
  std::string status_message;
  int end_call_total = 0;
  std::optional<SpanEndResult> end_result;
};

struct StartedSpanRecord {
  SpanDescriptor descriptor;
  std::optional<TraceContext> explicit_parent;
  std::optional<TraceContext> resolved_parent;
  std::shared_ptr<RecordingSpan> span;
};

class RecordingTracer final : public ITracer {
 public:
  [[nodiscard]] std::shared_ptr<ISpan> start_span(
      const SpanDescriptor& descriptor,
      const TraceContext* parent) override {
    std::optional<TraceContext> explicit_parent;
    if (parent != nullptr) {
      explicit_parent = *parent;
    }

    std::optional<TraceContext> resolved_parent;
    if (parent != nullptr && parent->state == TraceContextState::Active) {
      resolved_parent = *parent;
    } else if (active_context_.state == TraceContextState::Active) {
      resolved_parent = active_context_;
    }

    TraceContext context{
        .trace_id = resolved_parent.has_value()
                        ? resolved_parent->trace_id
                        : hex_id(++trace_seed_,
                                 dasall::infra::tracing::kTraceIdHexLength),
        .span_id = hex_id(++span_seed_,
                          dasall::infra::tracing::kSpanIdHexLength),
        .trace_flags = 0x01U,
        .trace_state = {},
        .parent_span_id = resolved_parent.has_value() ? resolved_parent->span_id
                                                      : std::string{},
        .state = TraceContextState::Active,
        .is_remote = false,
    };
    auto span = std::make_shared<RecordingSpan>(descriptor,
                                                context,
                                                resolved_parent);
    started_spans.push_back(StartedSpanRecord{
        .descriptor = descriptor,
        .explicit_parent = explicit_parent,
        .resolved_parent = resolved_parent,
        .span = span,
    });
    return span;
  }

  void with_active_span(const std::shared_ptr<ISpan>& span,
                        const ActiveSpanCallback& fn) override {
    const auto recording_span = std::dynamic_pointer_cast<RecordingSpan>(span);
    const auto previous = active_context_;
    active_context_ = recording_span != nullptr ? recording_span->context
                                                : TraceContext::noop();
    fn();
    active_context_ = previous;
  }

  [[nodiscard]] TraceContext current_context() const override {
    return active_context_;
  }

  std::vector<StartedSpanRecord> started_spans;

 private:
  TraceContext active_context_ = TraceContext::noop();
  std::uint64_t trace_seed_ = 0U;
  std::uint64_t span_seed_ = 0U;
};

class RecordingTracerProvider final : public ITracerProvider {
 public:
  TraceOperationStatus init(const TraceConfig&) override {
    return TraceOperationStatus::success("trace://services/smoke-provider-init");
  }

  [[nodiscard]] std::shared_ptr<ITracer> get_tracer(
      const TracerScope& scope) override {
    last_scope = scope;
    if (tracer == nullptr) {
      tracer = std::make_shared<RecordingTracer>();
    }
    return tracer;
  }

  TraceOperationStatus force_flush(std::uint32_t) override {
    return TraceOperationStatus::success("trace://services/smoke-provider-flush");
  }

  TraceOperationStatus shutdown(std::uint32_t) override {
    return TraceOperationStatus::success("trace://services/smoke-provider-shutdown");
  }

  std::shared_ptr<RecordingTracer> tracer;
  TracerScope last_scope{};
};

[[nodiscard]] const std::string* string_attr(const TraceAttributeMap& attrs,
                                             std::string_view key) {
  const auto* attr = dasall::infra::tracing::find_trace_attribute(attrs, key);
  return attr != nullptr ? std::get_if<std::string>(attr) : nullptr;
}

void assert_trace_chain(const StartedSpanRecord& root,
                        const StartedSpanRecord& lane,
                        const StartedSpanRecord& adapter,
                        const StartedSpanRecord& external,
                        const std::string& root_name,
                        const std::string& lane_name,
                        const std::string& capability_id,
                        const std::string& target_id) {
  assert_equal(root_name,
               root.descriptor.name,
               "smoke tracing should preserve the expected facade span name");
  assert_equal(lane_name,
               lane.descriptor.name,
               "smoke tracing should preserve the expected lane span name");
  assert_true(root.descriptor.kind == SpanKind::Server &&
                  lane.descriptor.kind == SpanKind::Internal &&
                  adapter.descriptor.kind == SpanKind::Client &&
                  external.descriptor.kind == SpanKind::Client,
              "smoke tracing should keep facade/lane/adapter/external span kinds stable");
  assert_true(root.explicit_parent.has_value() && root.explicit_parent->is_remote,
              "smoke tracing should synthesize a remote tool parent for the facade span");
  assert_true(lane.resolved_parent.has_value() &&
                  lane.resolved_parent->span_id == root.span->context.span_id &&
                  adapter.resolved_parent.has_value() &&
                  adapter.resolved_parent->span_id == lane.span->context.span_id &&
                  external.resolved_parent.has_value() &&
                  external.resolved_parent->span_id == adapter.span->context.span_id,
              "smoke tracing should keep lane, adapter, and external spans strictly nested");
  const auto* request_id = string_attr(root.descriptor.attrs, "services.request_id");
  const auto* tool_call_id = string_attr(root.descriptor.attrs, "services.tool_call_id");
  const auto* traced_capability = string_attr(lane.descriptor.attrs,
                                              "services.capability_id");
  const auto* traced_target = string_attr(lane.descriptor.attrs, "services.target_id");
  assert_true(request_id != nullptr && !request_id->empty() &&
                  tool_call_id != nullptr && !tool_call_id->empty() &&
                  traced_capability != nullptr && *traced_capability == capability_id &&
                  traced_target != nullptr && *traced_target == target_id,
              "smoke tracing should keep request, tool, capability, and target fields observable");
}

void test_capability_services_smoke_integration_registers_minimal_loopback_round_trip() {
  RecordingDispatchBackend* logging_backend = nullptr;
  const auto logger = make_logger(&logging_backend);
  dasall::services::internal::ServiceLoggingBridge logging_bridge(logger);

  dasall::tests::mocks::CapabilityServicesLoopbackFixtureOptions fixture_options;
  fixture_options.logging_bridge = &logging_bridge;
  CapabilityServicesLoopbackFixture fixture(std::move(fixture_options));

  const auto execute_result = fixture.execution_service().execute(
      fixture.make_execute_request());
  const auto query_result = fixture.data_service().query(
      fixture.make_query_request());
  const auto catalog_result = fixture.data_service().list_capabilities(
      fixture.make_catalog_request());
  assert_true(logger->flush(dasall::infra::logging::LogFlushDeadline{.timeout_ms = 500}).ok,
        "services smoke integration should flush the shared logger before inspecting dispatched services records");

  assert_true(!execute_result.error.has_value(),
              "smoke loopback execute should succeed without structured error");
  assert_true(!query_result.error.has_value(),
              "smoke loopback data query should succeed without structured error");
  assert_true(!catalog_result.error.has_value(),
              "smoke loopback catalog query should succeed without structured error");
  assert_true(execute_result.payload_json.find("\"applied\":true") != std::string::npos,
              "smoke execute should preserve the loopback action payload");
  assert_true(!execute_result.side_effects.empty(),
              "smoke execute should preserve side effect facts from the loopback adapter");
  assert_true(!query_result.from_cache,
              "first smoke query should be served from the live loopback path");
  assert_true(query_result.rows_json.find("\"projection\":\"status\"") !=
                  std::string::npos,
              "smoke query should preserve the requested projection in rows_json");
  assert_true(catalog_result.catalog_json.find("\"local_service\"") != std::string::npos,
              "smoke catalog query should advertise the local_service loopback route");
  assert_equal(3,
               static_cast<int>(fixture.local_requests().size()),
               "smoke integration should hit the local loopback adapter three times");
  assert_equal(0,
               static_cast<int>(fixture.remote_requests().size()),
               "smoke integration should not use remote fallback under default fixture policy");
  assert_equal(std::string("toggle"),
               fixture.local_requests().at(0).operation_name,
               "smoke execute should register the expected action name");
  assert_equal(3,
               static_cast<int>(logging_backend->events.size()),
               "smoke integration should emit one structured services log record for execute, query, and catalog routes");
  assert_true(has_log_attr(logging_backend->events.at(0).attrs,
                           "request_id",
                           "req-loopback-exec") &&
                  has_log_attr(logging_backend->events.at(0).attrs,
                               "capability_id",
                               "cap.exec") &&
                  has_log_attr(logging_backend->events.at(0).attrs,
                               "target_id",
                               "loopback.target"),
              "smoke execute should keep request_id, capability_id, and target_id observable on the structured services logging sink");
  assert_equal(std::string("status"),
               fixture.local_requests().at(1).operation_name,
               "smoke query should register the expected projection name");
  assert_equal(std::string("catalog.list"),
               fixture.local_requests().at(2).operation_name,
               "smoke catalog query should register the expected catalog operation name");
}

void test_capability_services_smoke_integration_exposes_audit_and_trace_fields() {
  RecordingAuditLogger audit_logger;
  dasall::services::internal::ServiceAuditBridge audit_bridge(&audit_logger);

  auto tracer_provider = std::make_shared<RecordingTracerProvider>();
  dasall::services::internal::ServiceTraceBridge trace_bridge(
      tracer_provider,
      dasall::services::internal::ServiceTraceBridgeOptions{
          .enabled = true,
          .profile_id = "desktop_full",
          .trace_sample_ratio = 1.0,
      });

    dasall::tests::mocks::CapabilityServicesLoopbackFixtureOptions fixture_options;
    fixture_options.high_risk_actions = {"toggle"};
    fixture_options.audit_bridge = &audit_bridge;
    fixture_options.trace_bridge = &trace_bridge;
    CapabilityServicesLoopbackFixture fixture(std::move(fixture_options));

  auto execute_request = fixture.make_execute_request("req-smoke-exec-observe",
                            "target-smoke-observe",
                            "toggle",
                            "{\"state\":\"on\"}");
  execute_request.idempotency_key = std::string("idem-smoke-observe");
  const auto execute_result = fixture.execution_service().execute(execute_request);
  const auto query_result = fixture.data_service().query(
      fixture.make_query_request("req-smoke-query-observe",
                                 "devices",
                                 "status"));

  assert_true(!execute_result.error.has_value() && !query_result.error.has_value(),
              "smoke observability flow should preserve successful execute/query results");
  assert_equal(2,
               static_cast<int>(audit_logger.events.size()),
               "high-risk smoke execute should emit requested/completed audit events");
  assert_equal(std::string("service.execution.requested"),
               audit_logger.events.front().action,
               "smoke audit should preserve the frozen requested event name");
  assert_equal(std::string("service.execution.completed"),
               audit_logger.events.back().action,
               "smoke audit should preserve the frozen completed event name");
  assert_equal(std::string("req-smoke-exec-observe"),
               audit_logger.contexts.front().request_id,
               "smoke audit should keep request_id observable in audit context");
  assert_equal(std::string("req-smoke-exec-observe.trace"),
               audit_logger.contexts.front().trace_id,
               "smoke audit should keep trace_id observable in audit context");
  assert_equal(std::string("services.execution"),
               audit_logger.contexts.front().worker_type,
               "smoke audit should keep the dedicated services worker type stable");
    assert_true(std::find(audit_logger.events.front().side_effects.begin(),
          audit_logger.events.front().side_effects.end(),
                std::string("request_id:req-smoke-exec-observe")) !=
        audit_logger.events.front().side_effects.end(),
              "smoke audit should surface request_id on the emitted side-effect facts");

  assert_true(trace_bridge.has_active_tracer(),
              "smoke trace should acquire a shared services tracer");
  assert_equal(std::string("services"),
               tracer_provider->last_scope.name,
               "smoke trace should request the frozen services tracer scope name");
  assert_equal(std::string("v1"),
               tracer_provider->last_scope.version,
               "smoke trace should request the frozen services tracer scope version");
  assert_true(tracer_provider->tracer != nullptr &&
                  tracer_provider->tracer->started_spans.size() == 8U,
              "smoke execute/query path should start facade/lane/adapter/external spans for both requests");

  const auto& execute_root = tracer_provider->tracer->started_spans[0];
  const auto& execute_lane = tracer_provider->tracer->started_spans[1];
  const auto& execute_adapter = tracer_provider->tracer->started_spans[2];
  const auto& execute_external = tracer_provider->tracer->started_spans[3];
  const auto& query_root = tracer_provider->tracer->started_spans[4];
  const auto& query_lane = tracer_provider->tracer->started_spans[5];
  const auto& query_adapter = tracer_provider->tracer->started_spans[6];
  const auto& query_external = tracer_provider->tracer->started_spans[7];

  assert_trace_chain(execute_root,
                     execute_lane,
                     execute_adapter,
                     execute_external,
                     "services.facade.execute",
                     "services.lane.execution.command.toggle",
                     "cap.exec",
                     "target-smoke-observe");
  assert_trace_chain(query_root,
                     query_lane,
                     query_adapter,
                     query_external,
                     "services.facade.query",
                     "services.lane.data.query.status",
                     "devices",
                     "devices");
  assert_true(execute_root.span->context.trace_id != query_root.span->context.trace_id,
              "distinct smoke requests should keep isolated trace roots");
  const auto trace_status = trace_bridge.get_status();
  assert_true(trace_status.has_consistent_state() && !trace_status.degraded &&
                  trace_status.started_span_total == 8U &&
                  trace_status.span_failure_total == 0U,
              "smoke trace bridge should remain healthy after execute/query observability checks");
}

}  // namespace

int main() {
  try {
    test_capability_services_smoke_integration_registers_minimal_loopback_round_trip();
    test_capability_services_smoke_integration_exposes_audit_and_trace_fields();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}