#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "tracing/ITracerProvider.h"

namespace dasall::infra::tracing {

struct TraceConfig {
  bool enabled = true;
  std::string provider_type = "internal";
  bool force_flush_on_stop = true;

  [[nodiscard]] bool is_valid() const;
};

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
  TracerMap tracers_;
};

}  // namespace dasall::infra::tracing