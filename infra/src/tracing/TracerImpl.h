#pragma once

#include <memory>
#include <optional>

#include "tracing/ITracer.h"
#include "tracing/SamplingPolicyEngine.h"

namespace dasall::infra::tracing {

class TracerImpl final : public ITracer {
 public:
    explicit TracerImpl(TracerScope scope, TraceConfig config = {});

  [[nodiscard]] std::shared_ptr<ISpan> start_span(
      const SpanDescriptor& descriptor,
      const TraceContext* parent) override;
  void with_active_span(
      const std::shared_ptr<ISpan>& span,
      const ActiveSpanCallback& fn) override;
  [[nodiscard]] TraceContext current_context() const override;

  [[nodiscard]] const TracerScope& scope() const;
  [[nodiscard]] const std::optional<TraceContext>& last_started_context() const;

 private:
  [[nodiscard]] TraceContext resolve_parent_context(const TraceContext* parent) const;

  TracerScope scope_;
    TraceConfig config_;
    SamplingPolicyEngine sampling_policy_;
  std::optional<TraceContext> last_started_context_;
};

}  // namespace dasall::infra::tracing