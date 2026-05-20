#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string_view>
#include <type_traits>
#include <vector>

#include "ServiceFacade.h"
#include "bridges/ServiceTraceBridge.h"
#include "support/TestAssertions.h"

namespace {

using dasall::infra::tracing::ActiveSpanCallback;
using dasall::infra::tracing::ISpan;
using dasall::infra::tracing::ITracer;
using dasall::infra::tracing::ITracerProvider;
using dasall::infra::tracing::SpanDescriptor;
using dasall::infra::tracing::SpanEndResult;
using dasall::infra::tracing::SpanStatusCode;
using dasall::infra::tracing::TraceAttributeMap;
using dasall::infra::tracing::TraceAttributeValue;
using dasall::infra::tracing::TraceConfig;
using dasall::infra::tracing::TraceContext;
using dasall::infra::tracing::TraceContextState;
using dasall::infra::tracing::TraceOperationStatus;
using dasall::infra::tracing::TracerScope;

class RecordingSpan final : public ISpan {
 public:
  RecordingSpan(SpanDescriptor descriptor, TraceContext context)
      : descriptor(std::move(descriptor)), context(std::move(context)) {}

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
    return SpanEndResult{
        .end_ts_unix_ms = end_ts_unix_ms.has_value()
                             ? end_ts_unix_ms
                             : std::optional<std::int64_t>(1712750000000LL),
        .status_code = status_code,
        .status_message = status_message,
        .dropped_attr_count = 0U,
    };
  }

  [[nodiscard]] TraceContext get_context() const override {
    return context;
  }

  SpanDescriptor descriptor;
  TraceContext context;
  TraceAttributeMap attributes;
  SpanStatusCode status_code = SpanStatusCode::Unset;
  std::string status_message;
  int end_call_total = 0;
};

struct StartedSpanRecord {
  SpanDescriptor descriptor;
  std::optional<TraceContext> explicit_parent;
  std::shared_ptr<RecordingSpan> span;
};

class RecordingTracer final : public ITracer {
 public:
  [[nodiscard]] std::shared_ptr<ISpan> start_span(
      const SpanDescriptor& descriptor,
      const TraceContext* parent) override {
    std::optional<TraceContext> explicit_parent = std::nullopt;
    if (parent != nullptr) {
      explicit_parent = *parent;
    }

    const auto suffix = static_cast<char>('1' + started_spans.size());
    TraceContext context{
        .trace_id = explicit_parent.has_value() ? explicit_parent->trace_id
                                                : std::string(32, '1'),
        .span_id = std::string(15, '0') + std::string(1, suffix),
        .trace_flags = 0x01U,
        .trace_state = {},
        .parent_span_id = explicit_parent.has_value() ? explicit_parent->span_id
                                                      : std::string{},
        .state = TraceContextState::Active,
        .is_remote = false,
    };
    auto span = std::make_shared<RecordingSpan>(descriptor, context);
    started_spans.push_back(StartedSpanRecord{
        .descriptor = descriptor,
        .explicit_parent = explicit_parent,
        .span = span,
    });
    return span;
  }

  void with_active_span(const std::shared_ptr<ISpan>&,
                        const ActiveSpanCallback& fn) override {
    fn();
  }

  [[nodiscard]] TraceContext current_context() const override {
    return TraceContext::noop();
  }

  std::vector<StartedSpanRecord> started_spans;
};

class RecordingTracerProvider final : public ITracerProvider {
 public:
  TraceOperationStatus init(const TraceConfig&) override {
    return TraceOperationStatus::success("trace://services/facade-test/init");
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
    return TraceOperationStatus::success("trace://services/facade-test/flush");
  }

  TraceOperationStatus shutdown(std::uint32_t) override {
    return TraceOperationStatus::success("trace://services/facade-test/shutdown");
  }

  std::shared_ptr<RecordingTracer> tracer;
  TracerScope last_scope{};
};

dasall::services::ServiceCallContext make_context() {
  dasall::contracts::RuntimeBudget budget;
  budget.max_latency_ms = 5000;

  return dasall::services::ServiceCallContext{
      .request_id = "req-010",
      .session_id = "session-010",
      .trace_id = "trace-010",
      .tool_call_id = "tool-call-010",
      .goal_id = "goal-010",
      .budget_guard = budget,
      .deadline_ms = 9000,
  };
}

dasall::services::ExecutionCommandRequest make_command_request() {
  return dasall::services::ExecutionCommandRequest{
      .context = make_context(),
      .target = {
          .capability_id = "cap.exec",
          .target_id = "target-010",
      },
      .action = "toggle",
      .arguments_json = "{}",
      .idempotency_key = std::string("idem-010"),
  };
}

dasall::services::DataQueryRequest make_data_request() {
  return dasall::services::DataQueryRequest{
      .context = make_context(),
      .dataset = "inventory",
      .filters_json = "{}",
      .projection = "summary",
      .freshness = dasall::services::ServiceDataFreshness::allow_stale,
  };
}

dasall::services::ExecutionSubscriptionRequest make_subscription_request() {
  return dasall::services::ExecutionSubscriptionRequest{
      .context = make_context(),
      .target = {
          .capability_id = "cap.exec",
          .target_id = "target-010",
      },
      .stream_kind = "status",
      .cursor = std::string("0"),
      .max_events = 2U,
  };
}

void test_service_facade_implements_public_interfaces_and_delegates() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;
  using Facade = dasall::services::internal::ServiceFacade;

  static_assert(std::is_base_of_v<dasall::services::IExecutionService, Facade>,
                "ServiceFacade should implement IExecutionService");
  static_assert(std::is_base_of_v<dasall::services::IDataService, Facade>,
                "ServiceFacade should implement IDataService");

  dasall::services::internal::ServiceContextBuilder builder;
  bool execute_called = false;
  bool query_called = false;
  dasall::services::ServiceCallContext execute_context;
  dasall::services::ServiceCallContext query_context;

    auto dependencies = dasall::services::internal::ServiceFacadeDependencies{};
    dependencies.context_builder = &builder;
    dependencies.execute_command =
      [&](const dasall::services::ServiceCallContext& context,
        const dasall::services::ExecutionCommandRequest& request) {
        execute_called = true;
        execute_context = context;
        assert_equal(std::string("toggle"), request.action,
                     "execute should forward the action unchanged");
        return dasall::services::ExecutionCommandResult{
          .code = std::nullopt,
            .execution_id = "exec-010",
            .payload_json = "{\"status\":\"ok\"}",
            .side_effects = {"state.changed"},
            .compensation_hints = {"state.restore"},
            .error = std::nullopt,
        };
      };
  dependencies.query_data = [&](const dasall::services::ServiceCallContext& context,
                                const dasall::services::DataQueryRequest& request) {
        query_called = true;
        query_context = context;
        assert_equal(std::string("inventory"), request.dataset,
                     "query should forward dataset unchanged");
        return dasall::services::DataQueryResult{
          .code = std::nullopt,
            .rows_json = "[]",
            .from_cache = true,
            .error = std::nullopt,
        };
          };

        dasall::services::internal::ServiceFacade facade(std::move(dependencies));

  dasall::services::IExecutionService* execution_service = &facade;
  dasall::services::IDataService* data_service = &facade;

  const auto execution_result = execution_service->execute(make_command_request());
  const auto data_result = data_service->query(make_data_request());

  assert_true(execute_called, "execute should delegate to the injected command handler");
  assert_true(query_called, "query should delegate to the injected data handler");
  assert_equal(std::string("exec-010"), execution_result.execution_id,
               "execute should return the delegated execution result");
  assert_true(data_result.from_cache, "query should return the delegated data result");
  assert_equal(std::string("req-010"), execute_context.request_id,
               "execute should receive normalized request_id");
  assert_equal(9000, static_cast<int>(execute_context.deadline_ms),
               "execute should receive normalized deadline_ms");
  assert_equal(std::string("goal-010"), query_context.goal_id,
               "query should receive normalized goal_id");
}

void test_service_facade_rejects_invalid_context_before_delegate() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  dasall::services::internal::ServiceContextBuilder builder;
  bool execute_called = false;

    auto dependencies = dasall::services::internal::ServiceFacadeDependencies{};
    dependencies.context_builder = &builder;
    dependencies.execute_command =
      [&](const dasall::services::ServiceCallContext&,
        const dasall::services::ExecutionCommandRequest&) {
        execute_called = true;
        return dasall::services::ExecutionCommandResult{};
      };

    dasall::services::internal::ServiceFacade facade(std::move(dependencies));

  auto invalid_request = make_command_request();
  invalid_request.context.request_id.clear();

  const auto result = facade.execute(invalid_request);

  assert_true(!execute_called,
              "execute should not reach the injected handler when context normalization fails");
  assert_equal(static_cast<int>(dasall::contracts::ResultCode::ValidationFieldMissing),
               static_cast<int>(result.code.value_or(dasall::contracts::ResultCode::ToolExecutionFailed)),
               "invalid context should surface a validation result code");
  assert_true(result.error.has_value(), "invalid context should surface structured error info");
  assert_equal(std::string("request_id is required"), result.error->details.message,
               "invalid context should preserve the builder error message");
}

void test_service_facade_subscribe_starts_facade_trace_span() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  dasall::services::internal::ServiceContextBuilder builder;
  auto provider = std::make_shared<RecordingTracerProvider>();
  dasall::services::internal::ServiceTraceBridge trace_bridge(
      provider,
      dasall::services::internal::ServiceTraceBridgeOptions{
          .enabled = true,
          .profile_id = "edge_balanced",
          .trace_sample_ratio = 1.0,
      });
  bool subscribe_called = false;
  dasall::services::ServiceCallContext subscribe_context;

  auto dependencies = dasall::services::internal::ServiceFacadeDependencies{};
  dependencies.context_builder = &builder;
  dependencies.subscribe_execution_state =
      [&](const dasall::services::ServiceCallContext& context,
          const dasall::services::ExecutionSubscriptionRequest& request) {
        subscribe_called = true;
        subscribe_context = context;
        assert_equal(std::string("status"),
                     request.stream_kind,
                     "subscribe should forward stream_kind unchanged");
        return dasall::services::ExecutionSubscriptionResult{
            .code = std::nullopt,
            .events_json = "[{\"seq\":1}]",
            .next_cursor = std::string("1"),
            .resync_required = false,
            .dropped_count = 0U,
            .error = std::nullopt,
        };
      };
  dependencies.trace_bridge = &trace_bridge;

  dasall::services::internal::ServiceFacade facade(std::move(dependencies));

  const auto result = facade.subscribe(make_subscription_request());

  assert_true(subscribe_called,
              "subscribe should delegate to the injected subscription handler");
  assert_equal(std::string("req-010"),
               subscribe_context.request_id,
               "subscribe should receive normalized request_id");
  assert_true(!result.error.has_value() && result.next_cursor.has_value() &&
                  *result.next_cursor == "1",
              "subscribe should preserve the delegated subscription result while tracing");
  assert_true(provider->tracer != nullptr &&
                  provider->tracer->started_spans.size() == 1U,
              "subscribe should start exactly one facade span when the trace bridge is enabled");
  const auto& record = provider->tracer->started_spans.front();
  assert_equal(std::string("services.facade.subscribe"),
               record.descriptor.name,
               "subscribe tracing should use the frozen services.facade.subscribe span name");
  assert_true(record.explicit_parent.has_value() && record.explicit_parent->is_remote,
              "subscribe facade spans should synthesize a remote tool parent context");
  assert_true(record.span->status_code == SpanStatusCode::Ok &&
                  record.span->end_call_total == 1,
              "successful subscribe tracing should end the facade span once with Ok status");
}

}  // namespace

int main() {
  try {
    test_service_facade_implements_public_interfaces_and_delegates();
    test_service_facade_rejects_invalid_context_before_delegate();
    test_service_facade_subscribe_starts_facade_trace_span();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}