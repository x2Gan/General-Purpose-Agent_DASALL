#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "tracing/ITracerProvider.h"

namespace dasall::infra::tracing {

class SpanProcessorPipeline;

class TracerProviderImpl final : public ITracerProvider {
 public:
  TracerProviderImpl() = default;

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

  LifecycleState lifecycle_state_ = LifecycleState::Created;
  std::optional<TraceConfig> last_config_;
  std::optional<TracerScope> last_scope_;
  std::shared_ptr<SpanProcessorPipeline> pipeline_;
  TracerMap tracers_;
};

}  // namespace dasall::infra::tracing