#pragma once

#include <string>
#include <string_view>

#include "tracing/TraceConfig.h"

namespace dasall::infra::tracing {

struct SamplingInput {
  std::string trace_id;
  std::string span_name;
  SpanKind span_kind = SpanKind::Internal;
  TraceAttributeMap attrs;
  TraceContext parent_context = TraceContext::invalid();

  [[nodiscard]] bool is_valid() const;
};

class SamplingPolicyEngine {
 public:
  explicit SamplingPolicyEngine(TraceSamplerConfig config = {});

  [[nodiscard]] SamplingDecision should_sample(const SamplingInput& input) const;
  [[nodiscard]] const TraceSamplerConfig& config() const;
  [[nodiscard]] std::string description() const;

 private:
  [[nodiscard]] SamplingDecision make_decision(SamplingDecisionKind decision,
                                               std::string reason) const;
  [[nodiscard]] static bool parent_is_sampled(const TraceContext& parent_context);
  [[nodiscard]] static std::uint64_t trace_id_suffix(std::string_view trace_id);

  TraceSamplerConfig config_;
};

}  // namespace dasall::infra::tracing