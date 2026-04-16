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

#include "ops/ToolTraceBridge.h"
#include "support/TestAssertions.h"

namespace {

using dasall::contracts::ResultCode;
using dasall::contracts::ToolInvocationKind;
using dasall::contracts::ToolRequest;
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
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;
using dasall::tools::ToolInvocationContext;
using dasall::tools::ops::ToolTraceBridge;
using dasall::tools::ops::ToolTraceBridgeOptions;
using dasall::tools::ops::ToolTraceStageDetails;

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
                               : std::optional<std::int64_t>(1712749000000LL),
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
    std::optional<TraceContext> explicit_parent = std::nullopt;
    if (parent != nullptr) {
      explicit_parent = *parent;
    }

    std::optional<TraceContext> resolved_parent = std::nullopt;
    if (parent != nullptr && parent->state == TraceContextState::Active) {
      resolved_parent = *parent;
    } else if (active_context_.state == TraceContextState::Active) {
      resolved_parent = active_context_;
    }

    TraceContext context{
        .trace_id = resolved_parent.has_value()
                        ? resolved_parent->trace_id
                        : hex_id(++trace_seed_, dasall::infra::tracing::kTraceIdHexLength),
        .span_id = hex_id(++span_seed_, dasall::infra::tracing::kSpanIdHexLength),
        .trace_flags = 0x01U,
        .trace_state = {},
        .parent_span_id = resolved_parent.has_value() ? resolved_parent->span_id
                                                      : std::string{},
        .state = TraceContextState::Active,
        .is_remote = false,
    };
    auto span = std::make_shared<RecordingSpan>(descriptor, context, resolved_parent);
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
    return TraceOperationStatus::success("trace://tools/provider-init");
  }

  [[nodiscard]] std::shared_ptr<ITracer> get_tracer(
      const TracerScope& scope) override {
    last_scope = scope;
    ++get_tracer_call_total;
    if (tracer == nullptr) {
      tracer = std::make_shared<RecordingTracer>();
    }
    return tracer;
  }

  TraceOperationStatus force_flush(std::uint32_t) override {
    return TraceOperationStatus::success("trace://tools/provider-flush");
  }

  TraceOperationStatus shutdown(std::uint32_t) override {
    return TraceOperationStatus::success("trace://tools/provider-shutdown");
  }

  std::shared_ptr<RecordingTracer> tracer;
  TracerScope last_scope{};
  std::uint64_t get_tracer_call_total = 0U;
};

[[nodiscard]] ToolInvocationContext make_context() {
  return ToolInvocationContext{
      .caller_domain = std::string("runtime.main"),
      .session_id = std::string("session-trace"),
      .profile_snapshot = nullptr,
      .trace = {
          .trace_id = std::string("trace-context"),
          .span_id = std::string("span-context"),
          .parent_span_id = std::nullopt,
      },
      .confirmation_facts = std::nullopt,
  };
}

[[nodiscard]] ToolRequest make_request() {
  return ToolRequest{
      .request_id = std::string("req-trace"),
      .tool_call_id = std::string("call-trace"),
      .tool_name = std::string("agent.terminal"),
      .invocation_kind = ToolInvocationKind::Action,
      .arguments_payload = std::string("{}"),
      .created_at = 1000,
      .goal_id = std::string("goal-trace"),
      .worker_task_id = std::string("worker-trace"),
      .runtime_budget = std::nullopt,
      .timeout_ms = 2500U,
      .idempotency_key = std::string("idem-trace"),
      .tags = std::vector<std::string>{"tools", "trace"},
  };
}

void test_tool_trace_bridge_builds_root_and_stage_parent_chain() {
  auto provider = std::make_shared<RecordingTracerProvider>();
  ToolTraceBridge bridge(
      provider,
      ToolTraceBridgeOptions{
          .enabled = true,
          .profile_id = "desktop_full",
          .trace_sample_ratio = 0.5,
          .tracer_scope_name = "tools",
          .tracer_scope_version = "v1",
          .schema_url = "https://opentelemetry.io/schemas/1.26.0",
      });

  const auto request = make_request();
  const auto context = make_context();
  auto root_span = bridge.start_root_span(request, context);
  assert_true(root_span.is_valid(),
              "tool trace bridge should create a valid root span when a tracer provider is configured");

  bridge.with_span(root_span, [&]() {
    auto validate_span = bridge.start_stage_span("tool.validate", request, context);
    bridge.with_span(validate_span, []() {});
    bridge.mark_success(&validate_span);

    auto execute_span = bridge.start_stage_span(
        "tool.execute.builtin",
        request,
        context,
        ToolTraceStageDetails{
            .route_kind = std::string("builtin"),
            .lane_key = std::string("builtin"),
            .server_id = std::nullopt,
            .reason_code = std::string("route.builtin.selected"),
        });
    bridge.with_span(execute_span, []() {});
    bridge.mark_error(&execute_span,
                      ResultCode::ProviderTimeout,
                      "provider timeout",
                      "tools.manager.execute");
  });
  bridge.mark_success(&root_span);

  assert_true(provider->tracer != nullptr,
              "tool trace bridge should request a tracer from the provider on first use");
  assert_equal(std::string("tools"),
               provider->last_scope.name,
               "tool trace bridge should use the frozen tools tracer scope");
  assert_equal(3,
               static_cast<int>(provider->tracer->started_spans.size()),
               "tool trace bridge should create one root span plus validate and builtin execute child spans");
  const auto& root_record = provider->tracer->started_spans[0];
  const auto& validate_record = provider->tracer->started_spans[1];
  const auto& execute_record = provider->tracer->started_spans[2];
  assert_equal(std::string("tool.invoke"),
               root_record.descriptor.name,
               "tool trace bridge should name the root span tool.invoke");
  assert_true(root_record.explicit_parent.has_value() && root_record.explicit_parent->is_remote,
              "tool trace root span should bind the runtime trace context as a remote parent");
  assert_equal(std::string("tool.validate"),
               validate_record.descriptor.name,
               "tool trace bridge should emit a validate child span");
  assert_true(validate_record.resolved_parent.has_value() &&
                  validate_record.resolved_parent->span_id == root_record.span->context.span_id,
              "tool validate span should inherit the invoke span as its active parent");
  assert_equal(std::string("tool.execute.builtin"),
               execute_record.descriptor.name,
               "tool trace bridge should emit a builtin execute child span");
  assert_true(execute_record.resolved_parent.has_value() &&
                  execute_record.resolved_parent->span_id == root_record.span->context.span_id,
              "tool execute span should inherit the invoke span as its active parent");
  assert_true(execute_record.span->status_code == SpanStatusCode::Error &&
                  execute_record.span->attributes.count("tools.error_stage") == 1U,
              "tool trace bridge should persist error status and error stage metadata before ending a failed span");
}

void test_tool_trace_bridge_degrades_when_provider_is_missing() {
  ToolTraceBridge bridge(
      nullptr,
      ToolTraceBridgeOptions{
          .enabled = true,
          .profile_id = "desktop_full",
          .trace_sample_ratio = 0.5,
          .tracer_scope_name = "tools",
          .tracer_scope_version = "v1",
          .schema_url = "https://opentelemetry.io/schemas/1.26.0",
      });

  auto root_span = bridge.start_root_span(make_request(), make_context());

  assert_true(!root_span.is_valid(),
              "tool trace bridge should return an invalid scope instead of blocking execution when tracing is unavailable");
  const auto status = bridge.get_status();
  assert_true(status.has_consistent_state() && bridge.is_degraded(),
              "missing tracer provider should be exposed as degraded trace bridge state");
  assert_true(status.span_failure_total == 1U,
              "missing tracer provider should increment span failure accounting");
}

}  // namespace

int main() {
  try {
    test_tool_trace_bridge_builds_root_and_stage_parent_chain();
    test_tool_trace_bridge_degrades_when_provider_is_missing();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}