#include "tracing/TracerProviderImpl.h"

#include <functional>
#include <string>
#include <utility>

#include "tracing/ITracer.h"
#include "tracing/TraceErrors.h"

namespace dasall::infra::tracing {
namespace {

constexpr std::string_view kTracerProviderSourceRef = "TracerProviderImpl";

[[nodiscard]] TraceOperationStatus make_trace_failure(
    TraceErrorCode code,
    std::string message,
    std::string stage) {
  const auto mapping = map_trace_error_code(code);
  return TraceOperationStatus::failure(mapping.result_code,
                                       std::move(message),
                                       std::move(stage),
                                       std::string(kTracerProviderSourceRef) + ":" +
                                           std::string(trace_error_code_name(code)));
}

class NoopTracer final : public ITracer {
 public:
  explicit NoopTracer(TracerScope scope)
      : scope_(std::move(scope)) {}

  [[nodiscard]] std::shared_ptr<ISpan> start_span(
      const SpanDescriptor& descriptor,
      const TraceContext* parent) override {
    (void)descriptor;
    (void)parent;
    return {};
  }

  void with_active_span(
      const std::shared_ptr<ISpan>& span,
      const ActiveSpanCallback& fn) override {
    (void)span;
    if (fn) {
      fn();
    }
  }

  [[nodiscard]] TraceContext current_context() const override {
    return TraceContext::noop();
  }

 private:
  TracerScope scope_;
};

}  // namespace

bool TraceConfig::is_valid() const {
  return !provider_type.empty() && is_printable_ascii(provider_type);
}

TraceOperationStatus TracerProviderImpl::init(const TraceConfig& config) {
  if (lifecycle_state_ != LifecycleState::Created) {
    return invalid_transition("init", "created");
  }

  if (!config.is_valid()) {
    return make_trace_failure(
        TraceErrorCode::ConfigInvalid,
        "tracing provider requires a non-empty printable provider type",
        "tracing.init");
  }

  last_config_ = config;
  last_scope_.reset();
  tracers_.clear();
  lifecycle_state_ = LifecycleState::Initialized;
  return TraceOperationStatus::success("tracing-provider://initialized");
}

std::shared_ptr<ITracer> TracerProviderImpl::get_tracer(const TracerScope& scope) {
  if (lifecycle_state_ != LifecycleState::Initialized || !scope.is_valid()) {
    return {};
  }

  const auto scope_key = make_scope_key(scope);
  const auto existing = tracers_.find(scope_key);
  if (existing != tracers_.end()) {
    last_scope_ = scope;
    return existing->second;
  }

  auto tracer = std::make_shared<NoopTracer>(scope);
  tracers_.emplace(scope_key, tracer);
  last_scope_ = scope;
  return tracer;
}

TraceOperationStatus TracerProviderImpl::force_flush(std::uint32_t timeout_ms) {
  if (lifecycle_state_ != LifecycleState::Initialized) {
    return make_trace_failure(
        TraceErrorCode::ProviderNotReady,
        "tracing provider must be initialized before force_flush()",
        "tracing.force_flush");
  }

  if (timeout_ms == 0U) {
    return make_trace_failure(
        TraceErrorCode::ExportTimeout,
        "tracing provider force_flush() timed out before processors could drain",
        "tracing.force_flush");
  }

  return TraceOperationStatus::success("tracing-provider://flushed");
}

TraceOperationStatus TracerProviderImpl::shutdown(std::uint32_t timeout_ms) {
  if (lifecycle_state_ != LifecycleState::Initialized) {
    return make_trace_failure(
        TraceErrorCode::ProviderNotReady,
        "tracing provider must be initialized before shutdown()",
        "tracing.shutdown");
  }

  if (timeout_ms == 0U) {
    return make_trace_failure(
        TraceErrorCode::ShutdownTimeout,
        "tracing provider shutdown() timed out before processors/exporters completed",
        "tracing.shutdown");
  }

  tracers_.clear();
  lifecycle_state_ = LifecycleState::Stopped;
  return TraceOperationStatus::success("tracing-provider://stopped");
}

std::string_view TracerProviderImpl::lifecycle_state_name() const {
  switch (lifecycle_state_) {
    case LifecycleState::Created:
      return "created";
    case LifecycleState::Initialized:
      return "initialized";
    case LifecycleState::Stopped:
      return "stopped";
  }

  return "unknown";
}

const std::optional<TraceConfig>& TracerProviderImpl::last_config() const {
  return last_config_;
}

const std::optional<TracerScope>& TracerProviderImpl::last_scope() const {
  return last_scope_;
}

std::size_t TracerProviderImpl::tracer_count() const {
  return tracers_.size();
}

TraceOperationStatus TracerProviderImpl::invalid_transition(
    std::string_view operation,
    std::string_view expected_state) const {
  return make_trace_failure(
      TraceErrorCode::ProviderNotReady,
      "invalid tracing provider lifecycle transition for operation " +
          std::string(operation) + ": expected state " +
          std::string(expected_state) + ", actual state " +
          std::string(lifecycle_state_name()),
      "tracing.lifecycle");
}

std::string TracerProviderImpl::make_scope_key(const TracerScope& scope) {
  return scope.name + "|" + scope.version + "|" + scope.schema_url;
}

}  // namespace dasall::infra::tracing