#include "LoggingHealthProbe.h"

#include <algorithm>
#include <chrono>
#include <string>
#include <string_view>
#include <utility>

namespace dasall::infra::logging {

namespace {

[[nodiscard]] std::int64_t current_time_unix_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

[[nodiscard]] std::string make_detail_ref(std::string_view suffix) {
  return std::string(kLoggingHealthDetailNamespace) + "/" +
         std::string(suffix);
}

}  // namespace

LoggingHealthProbe::LoggingHealthProbe(
    std::shared_ptr<ILoggingHealthSignalProvider> signal_provider)
    : signal_provider_(std::move(signal_provider)) {}

ProbeDescriptor LoggingHealthProbe::make_descriptor() {
  return ProbeDescriptor{
      .probe_name = std::string(kLoggingHealthProbeName),
      .group = std::string(kLoggingHealthProbeGroup),
      .criticality = ProbeCriticality::Critical,
      .interval_ms = kLoggingHealthProbeIntervalMs,
      .timeout_ms = kLoggingHealthProbeTimeoutMs,
  };
}

ProbeStatus LoggingHealthProbe::map_status(const LoggingHealthSignals& signals) {
  if (signals.unrecoverable_failure_total > 0U) {
    return ProbeStatus::Unhealthy;
  }

  if (signals.fallback_active || signals.recovery_degraded ||
      signals.metrics_bridge_degraded || signals.dropped_total_delta > 0U ||
      signals.queue_above_high_watermark()) {
    return ProbeStatus::Degraded;
  }

  return ProbeStatus::Healthy;
}

std::string LoggingHealthProbe::detail_ref_for_state(
    const LoggingHealthSignals& signals,
    ProbeStatus status) {
  if (status == ProbeStatus::Healthy) {
    return make_detail_ref("healthy");
  }

  if (status == ProbeStatus::Unhealthy) {
    return make_detail_ref("unhealthy/unrecoverable_failure");
  }

  if (signals.fallback_active) {
    return make_detail_ref("degraded/fallback_active");
  }

  if (signals.recovery_degraded) {
    return make_detail_ref("degraded/recovery");
  }

  if (signals.metrics_bridge_degraded) {
    return make_detail_ref("degraded/metrics_bridge");
  }

  if (signals.dropped_total_delta > 0U) {
    return make_detail_ref("degraded/drop_total_delta");
  }

  if (signals.queue_above_high_watermark()) {
    return make_detail_ref("degraded/queue_depth");
  }

  return make_detail_ref("degraded/state");
}

ProbeResult LoggingHealthProbe::make_failure_result(
    contracts::ResultCode error_code,
    ProbeStatus status,
    std::int64_t latency_ms,
    std::int64_t timestamp_ms,
    std::string detail_ref) const {
  return ProbeResult{
      .probe_name = descriptor_.probe_name,
      .status = status,
      .latency_ms = std::max<std::int64_t>(0, latency_ms),
      .error_code = error_code,
      .detail_ref = detail_ref.empty() ? make_detail_ref("failure")
                                       : std::move(detail_ref),
      .timestamp = timestamp_ms > 0 ? timestamp_ms : current_time_unix_ms(),
  };
}

ProbeResult LoggingHealthProbe::probe() {
  if (!signal_provider_) {
    return make_failure_result(contracts::ResultCode::ValidationFieldMissing,
                               ProbeStatus::Unknown,
                               0,
                               current_time_unix_ms(),
                               make_detail_ref("config/provider_missing"));
  }

  const auto sample = signal_provider_->sample(descriptor_.timeout_ms);
  if (!sample.has_consistent_values()) {
    return make_failure_result(
        contracts::ResultCode::ValidationFieldMissing,
        ProbeStatus::Unknown,
        sample.latency_ms,
        sample.sampled_at_unix_ms,
        sample.detail_ref.empty() ? make_detail_ref("invalid/sample")
                                  : sample.detail_ref);
  }

  if (sample.state == LoggingHealthSampleState::Timeout) {
    return make_failure_result(
        contracts::ResultCode::ProviderTimeout,
        ProbeStatus::Degraded,
        sample.latency_ms > 0 ? sample.latency_ms : descriptor_.timeout_ms,
        sample.sampled_at_unix_ms,
        sample.detail_ref.empty() ? make_detail_ref("timeout")
                                  : sample.detail_ref);
  }

  if (sample.state == LoggingHealthSampleState::Invalid) {
    return make_failure_result(
        contracts::ResultCode::ValidationFieldMissing,
        ProbeStatus::Unknown,
        sample.latency_ms,
        sample.sampled_at_unix_ms,
        sample.detail_ref.empty() ? make_detail_ref("invalid/provider")
                                  : sample.detail_ref);
  }

  const auto status = map_status(sample.signals);
  return ProbeResult{
      .probe_name = descriptor_.probe_name,
      .status = status,
      .latency_ms = sample.latency_ms,
      .error_code = std::nullopt,
      .detail_ref = detail_ref_for_state(sample.signals, status),
      .timestamp = sample.sampled_at_unix_ms,
  };
}

}  // namespace dasall::infra::logging