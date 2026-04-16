#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "ToolInvocationContext.h"
#include "tool/ToolRequest.h"
#include "tracing/ISpan.h"
#include "tracing/ITracer.h"
#include "tracing/ITracerProvider.h"
#include "tracing/TraceTypes.h"

namespace dasall::tools::ops {

inline constexpr std::string_view kToolTraceDefaultScopeName = "tools";
inline constexpr std::string_view kToolTraceDefaultScopeVersion = "v1";
inline constexpr std::string_view kToolTraceDefaultSchemaUrl =
    "https://opentelemetry.io/schemas/1.26.0";
inline constexpr std::string_view kToolTraceDefaultDetailRef =
    "trace://tools/idle";

struct ToolTraceBridgeOptions {
  bool enabled = true;
  std::string profile_id = "unknown";
  double trace_sample_ratio = 0.0;
  std::string tracer_scope_name = std::string(kToolTraceDefaultScopeName);
  std::string tracer_scope_version = std::string(kToolTraceDefaultScopeVersion);
  std::string schema_url = std::string(kToolTraceDefaultSchemaUrl);
};

struct ToolTraceStageDetails {
  std::optional<std::string> route_kind;
  std::optional<std::string> lane_key;
  std::optional<std::string> server_id;
  std::optional<std::string> reason_code;
};

struct ToolTraceSpan {
  std::shared_ptr<infra::tracing::ITracer> tracer;
  std::shared_ptr<infra::tracing::ISpan> span;
  std::string detail_ref;

  [[nodiscard]] bool is_valid() const {
    return static_cast<bool>(tracer) && static_cast<bool>(span);
  }
};

struct ToolTraceBridgeStatus {
  std::uint64_t started_span_total = 0U;
  std::uint64_t span_failure_total = 0U;
  bool degraded = false;
  std::optional<contracts::ResultCode> last_error_code;
  std::string detail_ref = std::string(kToolTraceDefaultDetailRef);

  [[nodiscard]] bool has_consistent_state() const;
};

class ToolTraceBridge {
 public:
  explicit ToolTraceBridge(
      std::shared_ptr<infra::tracing::ITracerProvider> tracer_provider = nullptr,
      ToolTraceBridgeOptions options = {});

  void set_tracer_provider(
      std::shared_ptr<infra::tracing::ITracerProvider> tracer_provider,
      std::string profile_id = {});

  [[nodiscard]] bool has_active_tracer() const;
  [[nodiscard]] bool is_degraded() const;

  [[nodiscard]] ToolTraceSpan start_root_span(
      const contracts::ToolRequest& request,
      const ToolInvocationContext& context);
  [[nodiscard]] ToolTraceSpan start_stage_span(
      std::string_view span_name,
      const contracts::ToolRequest& request,
      const ToolInvocationContext& context,
      ToolTraceStageDetails details = {});

  template <typename Fn>
  auto with_span(ToolTraceSpan& scope, Fn&& fn) const -> std::invoke_result_t<Fn> {
    using Result = std::invoke_result_t<Fn>;

    if (!scope.is_valid()) {
      if constexpr (std::is_void_v<Result>) {
        std::forward<Fn>(fn)();
        return;
      } else {
        return std::forward<Fn>(fn)();
      }
    }

    if constexpr (std::is_void_v<Result>) {
      scope.tracer->with_active_span(scope.span, std::forward<Fn>(fn));
      return;
    } else {
      std::optional<Result> result;
      scope.tracer->with_active_span(scope.span, [&]() {
        result.emplace(std::forward<Fn>(fn)());
      });
      return std::move(*result);
    }
  }

  void mark_success(ToolTraceSpan* scope);
  void mark_error(ToolTraceSpan* scope,
                  contracts::ResultCode result_code,
                  std::string message,
                  std::string stage);

  [[nodiscard]] ToolTraceBridgeStatus get_status() const;

 private:
  [[nodiscard]] bool is_enabled() const;
  [[nodiscard]] infra::tracing::TracerScope make_scope() const;
  [[nodiscard]] infra::tracing::TraceContext make_runtime_parent_context(
      const contracts::ToolRequest& request,
      const ToolInvocationContext& context) const;
  [[nodiscard]] ToolTraceSpan start_span(
      infra::tracing::SpanDescriptor descriptor,
      std::string detail_ref,
      const infra::tracing::TraceContext* parent = nullptr);
  [[nodiscard]] std::shared_ptr<infra::tracing::ITracer> ensure_tracer();
  void safe_finish_span(ToolTraceSpan* scope,
                        infra::tracing::SpanStatusCode status_code,
                        std::string message,
                        contracts::ResultCode result_code);
  void record_failure(contracts::ResultCode result_code, std::string detail_ref);
  void clear_error_state(std::string detail_ref);

  std::shared_ptr<infra::tracing::ITracerProvider> tracer_provider_;
  mutable std::shared_ptr<infra::tracing::ITracer> tracer_;
  ToolTraceBridgeOptions options_{};
  std::uint64_t started_span_total_ = 0U;
  std::uint64_t span_failure_total_ = 0U;
  bool degraded_ = false;
  std::optional<contracts::ResultCode> last_error_code_;
  std::string last_detail_ref_ = std::string(kToolTraceDefaultDetailRef);
};

}  // namespace dasall::tools::ops