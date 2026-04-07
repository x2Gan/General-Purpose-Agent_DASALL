#include "tracing/TracerProviderImpl.h"

#include <chrono>
#include <string>
#include <utility>

#include "tracing/ITracer.h"
#include "tracing/SpanProcessorPipeline.h"
#include "tracing/TraceErrors.h"
#include "tracing/TracerImpl.h"

namespace dasall::infra::tracing {
namespace {

constexpr std::string_view kTracerProviderSourceRef = "TracerProviderImpl";

[[nodiscard]] std::int64_t current_time_unix_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

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

}  // namespace

TracerProviderImpl::TracerProviderImpl()
    : audit_bridge_(nullptr,
                    TraceAuditBridgeOptions{
                        .detail_ref_prefix = "status://tracing/provider/audit/",
                        .event_id_prefix = "trace-provider-audit-event-",
                    }) {}

void TracerProviderImpl::set_metrics_provider(
    std::shared_ptr<metrics::IMetricsProvider> metrics_provider,
    std::string profile_id) {
  metrics_provider_ = std::move(metrics_provider);
  observability_profile_id_ = profile_id.empty() ? std::string("unknown")
                                                 : std::move(profile_id);
  bind_pipeline_observability();
}

void TracerProviderImpl::set_audit_logger(
    std::shared_ptr<audit::IAuditLogger> audit_logger,
    InfraContext infra_context) {
  audit_logger_ = std::move(audit_logger);
  observability_context_ = std::move(infra_context);
  audit_bridge_.set_audit_logger(audit_logger_);
  bind_pipeline_observability();
  if (lifecycle_state_ == LifecycleState::Initialized && last_config_.has_value() &&
      !sampler_change_audited_) {
    emit_sampler_change_audit(
        std::string("uninitialized"),
        last_config_->sampler.type,
        "audit sink attached after tracing provider initialization");
  }
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

  const auto previous_sampler_type =
      last_config_.has_value() ? last_config_->sampler.type : std::string("uninitialized");
  last_config_ = config;
  last_scope_.reset();
  tracers_.clear();
  pipeline_ = std::make_shared<SpanProcessorPipeline>(config);
  bind_pipeline_observability();
  lifecycle_state_ = LifecycleState::Initialized;
  emit_sampler_change_audit(previous_sampler_type,
                            config.sampler.type,
                            "tracing provider initialized with sampler config");
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

  auto tracer = std::make_shared<TracerImpl>(scope,
                                             last_config_.value_or(TraceConfig{}),
                                             pipeline_);
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

  if (!pipeline_) {
    return TraceOperationStatus::success("tracing-provider://flushed");
  }

  return pipeline_->force_flush(timeout_ms);
}

TraceOperationStatus TracerProviderImpl::shutdown(std::uint32_t timeout_ms) {
  if (lifecycle_state_ != LifecycleState::Initialized) {
    return make_trace_failure(
        TraceErrorCode::ProviderNotReady,
        "tracing provider must be initialized before shutdown()",
        "tracing.shutdown");
  }

  if (timeout_ms == 0U) {
    const auto status = make_trace_failure(
        TraceErrorCode::ShutdownTimeout,
        "tracing provider shutdown() timed out before processors/exporters completed",
        "tracing.shutdown");
    emit_shutdown_fallback_audit(status);
    return status;
  }

  if (pipeline_) {
    const auto shutdown_status = pipeline_->shutdown(
        timeout_ms,
        last_config_.value_or(TraceConfig{}).force_flush_on_stop);
    if (!shutdown_status.ok) {
      return shutdown_status;
    }
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

TraceOperationStatus TracerProviderImpl::last_pipeline_status() const {
  return pipeline_ ? pipeline_->last_status()
                   : TraceOperationStatus::success("trace-pipeline://idle");
}

ExportBatchReport TracerProviderImpl::last_export_report() const {
  return pipeline_ ? pipeline_->exporter().last_report() : ExportBatchReport{};
}

TraceModuleSnapshot TracerProviderImpl::module_snapshot() const {
  if (pipeline_) {
    return pipeline_->module_snapshot();
  }

  return TraceModuleSnapshot{.queue_depth = 0U,
                             .dropped_total = 0U,
                             .exporter_state = "uninitialized",
                             .degraded = false};
}

TraceHealthSnapshot TracerProviderImpl::health_snapshot() const {
  if (pipeline_) {
    return pipeline_->health_snapshot();
  }

  return TraceHealthSnapshot{};
}

std::uint64_t TracerProviderImpl::export_success_total() const {
  return pipeline_ ? pipeline_->exporter().export_success_total() : 0U;
}

std::uint64_t TracerProviderImpl::export_failure_total() const {
  return pipeline_ ? pipeline_->exporter().export_failure_total() : 0U;
}

std::string TracerProviderImpl::last_rendered_output() const {
  return pipeline_ ? pipeline_->exporter().last_rendered_output() : std::string();
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

TraceAuditContext TracerProviderImpl::make_audit_context(std::string trace_id) const {
  TraceAuditContext context{
      .infra_context = observability_context_,
      .worker_type = std::string("infra.tracing"),
  };
  if (!trace_id.empty()) {
    context.infra_context.trace_id = std::move(trace_id);
  }

  return context;
}

void TracerProviderImpl::bind_pipeline_observability() {
  if (!pipeline_) {
    return;
  }

  pipeline_->set_metrics_provider(metrics_provider_, observability_profile_id_);
  pipeline_->set_audit_logger(audit_logger_, make_audit_context());
}

void TracerProviderImpl::emit_sampler_change_audit(std::string previous_sampler_type,
                                                   std::string current_sampler_type,
                                                   std::string reason) {
  if (!audit_bridge_.has_audit_logger() || !pipeline_) {
    sampler_change_audited_ = false;
    return;
  }

  const auto result = audit_bridge_.write_audit_event(TraceAuditEvent{
      .kind = TraceAuditEventKind::SamplerConfigChange,
      .action = std::string("sampler_changed"),
      .stage = std::string("tracing.init"),
      .outcome = TraceAuditEventOutcome::Success,
      .reason = std::move(reason),
      .error_code = std::nullopt,
      .module_snapshot = pipeline_->module_snapshot(),
      .context = make_audit_context(),
      .detail_ref = std::string("status://tracing/config/sampler/") +
                    current_sampler_type,
      .current_sampler_type = std::move(current_sampler_type),
      .previous_sampler_type = std::move(previous_sampler_type),
      .timestamp_ms = current_time_unix_ms(),
  });
  sampler_change_audited_ = result.emitted;
}

void TracerProviderImpl::emit_shutdown_fallback_audit(
    const TraceOperationStatus& status) {
  if (!audit_bridge_.has_audit_logger()) {
    return;
  }

  const auto error_code = TraceErrorCode::ShutdownTimeout;
  (void)audit_bridge_.write_audit_event(TraceAuditEvent{
      .kind = TraceAuditEventKind::ShutdownFallback,
      .action = std::string("shutdown_force_fallback"),
      .stage = status.error.has_value() ? status.error->details.stage
                                        : std::string("tracing.shutdown"),
      .outcome = TraceAuditEventOutcome::Failure,
      .reason = status.error.has_value()
                    ? status.error->details.message
                    : std::string(
                          "tracing provider entered shutdown fallback handling after a failed stop request"),
      .error_code = error_code,
      .module_snapshot = module_snapshot(),
      .context = make_audit_context(),
      .detail_ref = std::string("status://tracing/shutdown/provider_fallback"),
      .current_sampler_type = std::string(),
      .previous_sampler_type = std::string(),
      .timestamp_ms = current_time_unix_ms(),
  });
}

}  // namespace dasall::infra::tracing