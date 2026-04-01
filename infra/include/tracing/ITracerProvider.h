#pragma once

#include <cstdint>
#include <memory>

namespace dasall::infra::tracing {

class ITracer;
struct TraceConfig;
struct TraceOperationStatus;
struct TracerScope;

class ITracerProvider {
 public:
  virtual ~ITracerProvider() = default;

  virtual TraceOperationStatus init(const TraceConfig& config) = 0;
  [[nodiscard]] virtual std::shared_ptr<ITracer> get_tracer(const TracerScope& scope) = 0;
  virtual TraceOperationStatus force_flush(std::uint32_t timeout_ms) = 0;
  virtual TraceOperationStatus shutdown(std::uint32_t timeout_ms) = 0;
};

}  // namespace dasall::infra::tracing
