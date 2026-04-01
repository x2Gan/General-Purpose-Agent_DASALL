#pragma once

#include "tracing/TraceTypes.h"

namespace dasall::infra::tracing {

class ITraceContextPropagator {
 public:
  virtual ~ITraceContextPropagator() = default;

  virtual void inject(const TraceContext& context, TraceCarrier& carrier) const = 0;
  [[nodiscard]] virtual TraceContext extract(const TraceCarrier& carrier) const = 0;
};

}  // namespace dasall::infra::tracing
