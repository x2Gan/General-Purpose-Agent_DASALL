#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "InfraContext.h"
#include "audit/IAuditLogger.h"
#include "metrics/IMetricsProvider.h"
#include "tracing/TraceAuditBridge.h"
#include "tracing/ITracerProvider.h"
#include "tracing/TraceHealthProbe.h"

namespace dasall::infra::tracing {

class SpanProcessorPipeline;

class TracerProviderImpl final : public ITracerProvider {
 public:
  TracerProviderImpl();

  void set_metrics_provider(
      std::shared_ptr<metrics::IMetricsProvider> metrics_provider,
      std::string profile_id = "unknown");
  void set_audit_logger(std::shared_ptr<audit::IAuditLogger> audit_logger,
                        InfraContext infra_context = {});

  TraceOperationStatus init(const TraceConfig& config) override;
  [[nodiscard]] std::shared_ptr<ITracer> get_tracer(const TracerScope& scope) override;
  TraceOperationStatus force_flush(std::uint32_t timeout_ms) override;
  TraceOperationStatus shutdown(std::uint32_t timeout_ms) override;

  [[nodiscard]] std::string_view lifecycle_state_name() const;
  [[nodiscard]] const std::optional<TraceConfig>& last_config() const;
  [[nodiscard]] const std::optional<TracerScope>& last_scope() const;
  [[nodiscard]] std::size_t tracer_count() const;
  [[nodiscard]] TraceOperationStatus last_pipeline_status() const;
  [[nodiscard]] ExportBatchReport last_export_report() const;
  [[nodiscard]] TraceModuleSnapshot module_snapshot() const;
  [[nodiscard]] TraceHealthSnapshot health_snapshot() const;
  [[nodiscard]] std::uint64_t export_success_total() const;
  [[nodiscard]] std::uint64_t export_failure_total() const;
  [[nodiscard]] std::string last_rendered_output() const;

 private:
  enum class LifecycleState {
    Created,
    Initialized,
    Stopped,
  };

  using TracerMap = std::map<std::string, std::shared_ptr<ITracer>>;

  [[nodiscard]] TraceOperationStatus invalid_transition(
      std::string_view operation,
      std::string_view expected_state) const;
  [[nodiscard]] static std::string make_scope_key(const TracerScope& scope);
  [[nodiscard]] TraceAuditContext make_audit_context(
      std::string trace_id = std::string()) const;
  void bind_pipeline_observability();
  void emit_sampler_change_audit(std::string previous_sampler_type,
                                 std::string current_sampler_type,
                                 std::string reason);
  void emit_shutdown_fallback_audit(const TraceOperationStatus& status);

  LifecycleState lifecycle_state_ = LifecycleState::Created;
  std::optional<TraceConfig> last_config_;
  std::optional<TracerScope> last_scope_;
  std::shared_ptr<SpanProcessorPipeline> pipeline_;
  TracerMap tracers_;
  std::shared_ptr<metrics::IMetricsProvider> metrics_provider_;
  std::shared_ptr<audit::IAuditLogger> audit_logger_;
  std::string observability_profile_id_ = "unknown";
  InfraContext observability_context_{};
  TraceAuditBridge audit_bridge_;
  bool sampler_change_audited_ = false;
};

}  // namespace dasall::infra::tracing