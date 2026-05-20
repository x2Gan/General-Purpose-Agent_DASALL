#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "ServiceTypes.h"
#include "tracing/ISpan.h"
#include "tracing/ITracer.h"
#include "tracing/ITracerProvider.h"
#include "tracing/TraceTypes.h"

namespace dasall::services::internal {

struct AdapterInvocationRequest;
struct AdapterReceipt;
struct AdapterSelection;

inline constexpr std::string_view kServiceTraceDefaultScopeName = "services";
inline constexpr std::string_view kServiceTraceDefaultScopeVersion = "v1";
inline constexpr std::string_view kServiceTraceDefaultSchemaUrl =
    "https://opentelemetry.io/schemas/1.26.0";

struct ServiceTraceBridgeOptions {
  bool enabled = true;
  std::string profile_id = "unknown";
  double trace_sample_ratio = 0.0;
  std::string tracer_scope_name = std::string(kServiceTraceDefaultScopeName);
  std::string tracer_scope_version = std::string(kServiceTraceDefaultScopeVersion);
  std::string schema_url = std::string(kServiceTraceDefaultSchemaUrl);
};

struct ServiceTraceSpan {
  std::shared_ptr<infra::tracing::ITracer> tracer;
  std::shared_ptr<infra::tracing::ISpan> span;
  std::string detail_ref;

  [[nodiscard]] bool is_valid() const {
    return static_cast<bool>(tracer) && static_cast<bool>(span);
  }
};

struct ServiceTraceBridgeStatus {
  std::uint64_t started_span_total = 0U;
  std::uint64_t span_failure_total = 0U;
  bool degraded = false;
  std::optional<contracts::ResultCode> last_error_code;
  std::string detail_ref = "trace://services/idle";

  [[nodiscard]] bool has_consistent_state() const;
};

class ServiceTraceBridge {
 public:
  explicit ServiceTraceBridge(
      std::shared_ptr<infra::tracing::ITracerProvider> tracer_provider = nullptr,
      ServiceTraceBridgeOptions options = {});

  void set_tracer_provider(
      std::shared_ptr<infra::tracing::ITracerProvider> tracer_provider,
      std::string profile_id = {});

  [[nodiscard]] bool has_active_tracer() const;
  [[nodiscard]] bool is_degraded() const;

  [[nodiscard]] ServiceTraceSpan start_facade_span(
      const ServiceCallContext& context,
      std::string_view operation);
  [[nodiscard]] ServiceTraceSpan start_lane_span(
      const ServiceCallContext& context,
      std::string_view lane_name,
      std::string_view operation,
      const CapabilityTargetRef* target = nullptr);
  [[nodiscard]] ServiceTraceSpan start_adapter_span(
      const AdapterSelection& selection,
      const AdapterInvocationRequest& request);
  [[nodiscard]] ServiceTraceSpan start_external_span(
      const AdapterSelection& selection,
      const AdapterInvocationRequest& request);

  template <typename Fn>
  auto with_span(ServiceTraceSpan& scope, Fn&& fn) const
      -> std::invoke_result_t<Fn> {
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

  void complete_span(ServiceTraceSpan* scope,
                     const ExecutionCommandResult& result);
  void complete_span(ServiceTraceSpan* scope,
                     const ExecutionQueryResult& result);
  void complete_span(ServiceTraceSpan* scope,
                     const ExecutionSubscriptionResult& result);
  void complete_span(ServiceTraceSpan* scope,
                     const ExecutionDiagnoseResult& result);
  void complete_span(ServiceTraceSpan* scope,
                     const DataQueryResult& result);
  void complete_span(ServiceTraceSpan* scope,
                     const DataCatalogResult& result);
  void complete_span(ServiceTraceSpan* scope,
                     const AdapterReceipt& receipt);

  void mark_success(ServiceTraceSpan* scope);
  void mark_error(ServiceTraceSpan* scope,
                  contracts::ResultCode result_code,
                  std::string message,
                  std::string stage);

  [[nodiscard]] ServiceTraceBridgeStatus get_status() const;

 private:
  [[nodiscard]] bool is_enabled() const;
  [[nodiscard]] infra::tracing::TracerScope make_scope() const;
  [[nodiscard]] infra::tracing::TraceContext make_tool_parent_context(
      const ServiceCallContext& context) const;
  [[nodiscard]] ServiceTraceSpan start_span(
      infra::tracing::SpanDescriptor descriptor,
      std::string detail_ref,
      const infra::tracing::TraceContext* parent = nullptr);
  [[nodiscard]] std::shared_ptr<infra::tracing::ITracer> ensure_tracer();
  void safe_finish_span(ServiceTraceSpan* scope,
                        infra::tracing::SpanStatusCode status_code,
                        std::string message,
                        contracts::ResultCode result_code);
  void record_failure(contracts::ResultCode result_code,
                      std::string detail_ref);
  void clear_error_state(std::string detail_ref);

  std::shared_ptr<infra::tracing::ITracerProvider> tracer_provider_;
  mutable std::shared_ptr<infra::tracing::ITracer> tracer_;
  ServiceTraceBridgeOptions options_{};
  std::uint64_t started_span_total_ = 0U;
  std::uint64_t span_failure_total_ = 0U;
  bool degraded_ = false;
  std::optional<contracts::ResultCode> last_error_code_;
  std::string last_detail_ref_ = "trace://services/idle";
};

}  // namespace dasall::services::internal