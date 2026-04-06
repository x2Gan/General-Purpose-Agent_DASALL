#include "tracing/SamplingPolicyEngine.h"

#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>

namespace dasall::infra::tracing {
namespace {

[[nodiscard]] int hex_value(char ch) {
  if (ch >= '0' && ch <= '9') {
    return ch - '0';
  }

  if (ch >= 'a' && ch <= 'f') {
    return 10 + (ch - 'a');
  }

  return -1;
}

}  // namespace

bool SamplingInput::is_valid() const {
  return is_lower_hex_string(trace_id, kTraceIdHexLength) && !span_name.empty() &&
         is_printable_ascii(span_name) && trace_attributes_are_serializable(attrs) &&
         (parent_context.state != TraceContextState::Active || parent_context.is_valid());
}

SamplingPolicyEngine::SamplingPolicyEngine(TraceSamplerConfig config)
    : config_(std::move(config)) {}

SamplingDecision SamplingPolicyEngine::should_sample(const SamplingInput& input) const {
  if (!config_.is_valid() || !input.is_valid()) {
    return SamplingDecision{
        .decision = SamplingDecisionKind::Drop,
        .reason = "config_invalid",
        .sampler_desc = "InvalidSampler",
    };
  }

  if (config_.type == kTraceSamplerTypeAlwaysOn) {
    return make_decision(SamplingDecisionKind::RecordAndSample, "always_on");
  }

  if (config_.type == kTraceSamplerTypeAlwaysOff) {
    return make_decision(SamplingDecisionKind::Drop, "always_off");
  }

  if (config_.type == kTraceSamplerTypeParentBased ||
      config_.type == kTraceSamplerTypeParentBasedAlwaysOn) {
    if (input.parent_context.state == TraceContextState::Active && input.parent_context.is_valid()) {
      return make_decision(parent_is_sampled(input.parent_context)
                               ? SamplingDecisionKind::RecordAndSample
                               : SamplingDecisionKind::Drop,
                           parent_is_sampled(input.parent_context) ? "parent_sampled"
                                                                   : "parent_not_sampled");
    }

    return make_decision(SamplingDecisionKind::RecordAndSample, "root_always_on");
  }

  if (config_.ratio <= 0.0) {
    return make_decision(SamplingDecisionKind::Drop, "ratio_zero");
  }

  if (config_.ratio >= 1.0) {
    return make_decision(SamplingDecisionKind::RecordAndSample, "ratio_one");
  }

  const long double threshold =
      config_.ratio * static_cast<long double>(std::numeric_limits<std::uint64_t>::max());
  const auto sampled = static_cast<long double>(trace_id_suffix(input.trace_id)) <= threshold;
  return make_decision(sampled ? SamplingDecisionKind::RecordAndSample
                               : SamplingDecisionKind::Drop,
                       sampled ? "ratio_sampled" : "ratio_dropped");
}

const TraceSamplerConfig& SamplingPolicyEngine::config() const {
  return config_;
}

std::string SamplingPolicyEngine::description() const {
  if (config_.type == kTraceSamplerTypeAlwaysOn) {
    return "AlwaysOnSampler";
  }

  if (config_.type == kTraceSamplerTypeAlwaysOff) {
    return "AlwaysOffSampler";
  }

  if (config_.type == kTraceSamplerTypeParentBased ||
      config_.type == kTraceSamplerTypeParentBasedAlwaysOn) {
    return "ParentBased{root=AlwaysOnSampler}";
  }

  std::ostringstream stream;
  stream << "TraceIdRatioBased{" << std::fixed << std::setprecision(6) << config_.ratio << "}";
  return stream.str();
}

SamplingDecision SamplingPolicyEngine::make_decision(SamplingDecisionKind decision,
                                                     std::string reason) const {
  return SamplingDecision{
      .decision = decision,
      .reason = std::move(reason),
      .sampler_desc = description(),
  };
}

bool SamplingPolicyEngine::parent_is_sampled(const TraceContext& parent_context) {
  return (parent_context.trace_flags & 0x01U) != 0U;
}

std::uint64_t SamplingPolicyEngine::trace_id_suffix(std::string_view trace_id) {
  std::uint64_t value = 0;
  const auto start_offset = trace_id.size() > 16U ? trace_id.size() - 16U : 0U;
  for (std::size_t index = start_offset; index < trace_id.size(); ++index) {
    value <<= 4U;
    value |= static_cast<std::uint64_t>(hex_value(trace_id[index]));
  }

  return value;
}

}  // namespace dasall::infra::tracing