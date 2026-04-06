#pragma once

#include <cstdint>

#include "tracing/ITraceContextPropagator.h"

namespace dasall::infra::tracing {

class ContextPropagationAdapter final : public ITraceContextPropagator {
 public:
  void inject(const TraceContext& context, TraceCarrier& carrier) const override;
  [[nodiscard]] TraceContext extract(const TraceCarrier& carrier) const override;

  [[nodiscard]] const TraceOperationStatus& last_operation_status() const;
  [[nodiscard]] std::uint64_t invalid_context_total() const;

 private:
  void set_last_operation_status(TraceOperationStatus status) const;

  mutable TraceOperationStatus last_operation_status_ =
      TraceOperationStatus::success("trace-propagation://idle");
  mutable std::uint64_t invalid_context_total_ = 0;
};

}  // namespace dasall::infra::tracing