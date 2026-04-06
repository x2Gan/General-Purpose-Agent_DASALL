#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "tracing/TraceTypes.h"

namespace dasall::infra::tracing {

inline constexpr std::string_view kTraceProviderTypeInternal = "internal";
inline constexpr std::string_view kTraceProviderTypeOtelSdk = "otel_sdk";

inline constexpr std::string_view kTraceSamplerTypeAlwaysOn = "always_on";
inline constexpr std::string_view kTraceSamplerTypeAlwaysOff = "always_off";
inline constexpr std::string_view kTraceSamplerTypeRatio = "ratio";
inline constexpr std::string_view kTraceSamplerTypeParentBased = "parent_based";
inline constexpr std::string_view kTraceSamplerTypeParentBasedAlwaysOn = "parent_based_always_on";

inline constexpr std::string_view kTraceExporterTypeNoop = "noop";
inline constexpr std::string_view kTraceExporterTypeFile = "file";
inline constexpr std::string_view kTraceExporterTypeOtlp = "otlp";

inline constexpr std::string_view kTraceOverflowPolicyBlock = "block";
inline constexpr std::string_view kTraceOverflowPolicyDropOldest = "drop_oldest";

[[nodiscard]] inline bool is_supported_trace_provider_type(std::string_view provider_type) {
  return provider_type == kTraceProviderTypeInternal || provider_type == kTraceProviderTypeOtelSdk;
}

[[nodiscard]] inline bool is_supported_trace_sampler_type(std::string_view sampler_type) {
  return sampler_type == kTraceSamplerTypeAlwaysOn ||
         sampler_type == kTraceSamplerTypeAlwaysOff ||
         sampler_type == kTraceSamplerTypeRatio ||
         sampler_type == kTraceSamplerTypeParentBased ||
         sampler_type == kTraceSamplerTypeParentBasedAlwaysOn;
}

[[nodiscard]] inline bool is_supported_trace_exporter_type(std::string_view exporter_type) {
  return exporter_type == kTraceExporterTypeNoop || exporter_type == kTraceExporterTypeFile ||
         exporter_type == kTraceExporterTypeOtlp;
}

[[nodiscard]] inline bool is_supported_trace_overflow_policy(std::string_view overflow_policy) {
  return overflow_policy == kTraceOverflowPolicyBlock ||
         overflow_policy == kTraceOverflowPolicyDropOldest;
}

struct TraceSamplerConfig {
  std::string type = std::string(kTraceSamplerTypeParentBasedAlwaysOn);
  double ratio = 0.1;

  [[nodiscard]] bool is_valid() const {
    return is_supported_trace_sampler_type(type) && std::isfinite(ratio) && ratio >= 0.0 &&
           ratio <= 1.0;
  }
};

struct TraceBatchConfig {
  bool enabled = true;
  std::uint32_t max_queue_size = 2048;
  std::uint32_t max_export_batch_size = 512;
  std::uint32_t schedule_delay_ms = 5000;

  [[nodiscard]] bool is_valid() const {
    return max_queue_size > 0U && max_export_batch_size > 0U &&
           max_export_batch_size <= max_queue_size && schedule_delay_ms > 0U;
  }
};

struct TraceExporterConfig {
  std::string type = std::string(kTraceExporterTypeNoop);
  std::string otlp_endpoint;

  [[nodiscard]] bool is_valid() const {
    return is_supported_trace_exporter_type(type) &&
           (type != kTraceExporterTypeOtlp || !otlp_endpoint.empty());
  }
};

struct TraceSamplerConfigPatch {
  std::optional<std::string> type;
  std::optional<double> ratio;

  [[nodiscard]] bool has_values() const {
    return type.has_value() || ratio.has_value();
  }
};

struct TraceBatchConfigPatch {
  std::optional<bool> enabled;
  std::optional<std::uint32_t> max_queue_size;
  std::optional<std::uint32_t> max_export_batch_size;
  std::optional<std::uint32_t> schedule_delay_ms;

  [[nodiscard]] bool has_values() const {
    return enabled.has_value() || max_queue_size.has_value() ||
           max_export_batch_size.has_value() || schedule_delay_ms.has_value();
  }
};

struct TraceExporterConfigPatch {
  std::optional<std::string> type;
  std::optional<std::string> otlp_endpoint;

  [[nodiscard]] bool has_values() const {
    return type.has_value() || otlp_endpoint.has_value();
  }
};

struct TraceConfigPatch {
  std::optional<bool> enabled;
  std::optional<std::string> provider_type;
  TraceSamplerConfigPatch sampler;
  TraceBatchConfigPatch batch;
  std::optional<std::uint32_t> export_timeout_ms;
  TraceExporterConfigPatch exporter;
  std::optional<std::string> overflow_policy;
  std::optional<bool> force_flush_on_stop;

  [[nodiscard]] bool has_values() const {
    return enabled.has_value() || provider_type.has_value() || sampler.has_values() ||
           batch.has_values() || export_timeout_ms.has_value() || exporter.has_values() ||
           overflow_policy.has_value() || force_flush_on_stop.has_value();
  }
};

struct TraceConfig {
  bool enabled = true;
  std::string provider_type = std::string(kTraceProviderTypeInternal);
  TraceSamplerConfig sampler{};
  TraceBatchConfig batch{};
  std::uint32_t export_timeout_ms = 30000;
  TraceExporterConfig exporter{};
  std::string overflow_policy = std::string(kTraceOverflowPolicyDropOldest);
  bool force_flush_on_stop = true;

  [[nodiscard]] bool is_valid() const {
    return is_supported_trace_provider_type(provider_type) && sampler.is_valid() &&
           batch.is_valid() && export_timeout_ms > 0U && exporter.is_valid() &&
           is_supported_trace_overflow_policy(overflow_policy);
  }
};

inline void apply_trace_config_patch(TraceConfig& resolved, const TraceConfigPatch& patch) {
  if (patch.enabled.has_value()) {
    resolved.enabled = *patch.enabled;
  }

  if (patch.provider_type.has_value() && is_supported_trace_provider_type(*patch.provider_type)) {
    resolved.provider_type = *patch.provider_type;
  }

  if (patch.sampler.type.has_value() && is_supported_trace_sampler_type(*patch.sampler.type)) {
    resolved.sampler.type = *patch.sampler.type;
  }

  if (patch.sampler.ratio.has_value() && std::isfinite(*patch.sampler.ratio) &&
      *patch.sampler.ratio >= 0.0 && *patch.sampler.ratio <= 1.0) {
    resolved.sampler.ratio = *patch.sampler.ratio;
  }

  if (patch.batch.enabled.has_value()) {
    resolved.batch.enabled = *patch.batch.enabled;
  }

  if (patch.batch.max_queue_size.has_value() && *patch.batch.max_queue_size > 0U) {
    resolved.batch.max_queue_size = *patch.batch.max_queue_size;
  }

  if (patch.batch.max_export_batch_size.has_value() &&
      *patch.batch.max_export_batch_size > 0U) {
    resolved.batch.max_export_batch_size = *patch.batch.max_export_batch_size;
  }

  if (patch.batch.schedule_delay_ms.has_value() && *patch.batch.schedule_delay_ms > 0U) {
    resolved.batch.schedule_delay_ms = *patch.batch.schedule_delay_ms;
  }

  if (patch.export_timeout_ms.has_value() && *patch.export_timeout_ms > 0U) {
    resolved.export_timeout_ms = *patch.export_timeout_ms;
  }

  if (patch.exporter.type.has_value() &&
      is_supported_trace_exporter_type(*patch.exporter.type)) {
    resolved.exporter.type = *patch.exporter.type;
  }

  if (patch.exporter.otlp_endpoint.has_value()) {
    resolved.exporter.otlp_endpoint = *patch.exporter.otlp_endpoint;
  }

  if (patch.overflow_policy.has_value() &&
      is_supported_trace_overflow_policy(*patch.overflow_policy)) {
    resolved.overflow_policy = *patch.overflow_policy;
  }

  if (patch.force_flush_on_stop.has_value()) {
    resolved.force_flush_on_stop = *patch.force_flush_on_stop;
  }
}

[[nodiscard]] inline TraceConfig merge_trace_config(const TraceConfigPatch& profile,
                                                    const TraceConfigPatch& deployment,
                                                    const TraceConfigPatch& runtime,
                                                    TraceConfig defaults = {}) {
  apply_trace_config_patch(defaults, profile);
  apply_trace_config_patch(defaults, deployment);
  apply_trace_config_patch(defaults, runtime);

  if (!defaults.is_valid()) {
    return TraceConfig{};
  }

  return defaults;
}

}  // namespace dasall::infra::tracing