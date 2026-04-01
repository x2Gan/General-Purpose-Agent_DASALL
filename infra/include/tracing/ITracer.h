#pragma once

#include <functional>
#include <memory>

#include "tracing/TraceTypes.h"

namespace dasall::infra::tracing {

class ISpan;

using ActiveSpanCallback = std::function<void()>;

class ITracer {
 public:
  virtual ~ITracer() = default;

  [[nodiscard]] virtual std::shared_ptr<ISpan> start_span(
      const SpanDescriptor& descriptor,
      const TraceContext* parent) = 0;
  virtual void with_active_span(
      const std::shared_ptr<ISpan>& span,
      const ActiveSpanCallback& fn) = 0;
  [[nodiscard]] virtual TraceContext current_context() const = 0;
};

}  // namespace dasall::infra::tracing
