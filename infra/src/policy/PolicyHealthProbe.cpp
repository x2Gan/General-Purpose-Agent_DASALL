#include "PolicyHealthProbe.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <string>
#include <string_view>
#include <utility>

namespace dasall::infra::policy {

namespace {

[[nodiscard]] std::int64_t current_time_unix_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

[[nodiscard]] std::string make_detail_ref(std::string suffix) {
  return std::string(kPolicyHealthDetailNamespace) + "/" + suffix;
}

[[nodiscard]] std::string with_generation_suffix(std::string suffix,
                                                 std::uint64_t generation) {
  if (generation == 0U) {
    return suffix;
  }

  return suffix + "/generation/" + std::to_string(generation);
}

[[nodiscard]] std::string sanitize_detail_segment(std::string_view value) {
  std::string sanitized;
  sanitized.reserve(value.size());

  for (const unsigned char ch : value) {
    if (std::isalnum(ch) != 0 || ch == '_' || ch == '-') {
      sanitized.push_back(static_cast<char>(ch));
    } else {
      sanitized.push_back('_');
    }
  }

  if (sanitized.empty()) {
    return std::string("unknown");
  }

  return sanitized;
}

}  // namespace

PolicyHealthProbe::PolicyHealthProbe(
    std::shared_ptr<IPolicyHealthSignalProvider> signal_provider)
    : signal_provider_(std::move(signal_provider)) {}

ProbeDescriptor PolicyHealthProbe::make_descriptor() {
  return ProbeDescriptor{
      .probe_name = std::string(kPolicyHealthProbeName),
      .group = std::string(kPolicyHealthProbeGroup),
      .criticality = ProbeCriticality::Critical,
      .interval_ms = kPolicyHealthProbeIntervalMs,
      .timeout_ms = kPolicyHealthProbeTimeoutMs,
  };
}

ProbeStatus PolicyHealthProbe::map_status(const PolicyHealthSignals& signals) {
  if (!signals.has_current_snapshot() && !signals.has_last_known_good_snapshot()) {
    return ProbeStatus::Unhealthy;
  }

  if (!signals.has_current_snapshot()) {
    return ProbeStatus::Degraded;
  }

  if (signals.safe_mode || signals.audit_bridge_degraded ||
      signals.metrics_bridge_degraded || signals.has_recent_failure()) {
    return ProbeStatus::Degraded;
  }

  return ProbeStatus::Healthy;
}

std::string PolicyHealthProbe::detail_ref_for_state(
    const PolicyHealthSignals& signals,
    ProbeStatus status) {
  const std::uint64_t generation = signals.active_generation();
  if (status == ProbeStatus::Healthy) {
    return make_detail_ref(with_generation_suffix("ready", generation));
  }

  if (status == ProbeStatus::Unhealthy) {
    return make_detail_ref("unavailable/no_snapshot");
  }

  if (!signals.has_current_snapshot() && signals.has_last_known_good_snapshot()) {
    return make_detail_ref(
        with_generation_suffix("degraded/current_snapshot_missing", generation));
  }

  if (signals.safe_mode) {
    return make_detail_ref(with_generation_suffix("degraded/safe_mode", generation));
  }

  if (signals.last_policy_error_code.has_value()) {
    return make_detail_ref(with_generation_suffix(
        std::string("degraded/recent_failure/") +
            std::string(policy_error_code_name(*signals.last_policy_error_code)),
        generation));
  }

  if (!signals.last_failure_reason.empty()) {
    return make_detail_ref(with_generation_suffix(
        std::string("degraded/recent_failure/") +
            sanitize_detail_segment(signals.last_failure_reason),
        generation));
  }

  if (signals.metrics_bridge_degraded) {
    return make_detail_ref(
        with_generation_suffix("degraded/metrics_bridge", generation));
  }

  if (signals.audit_bridge_degraded) {
    return make_detail_ref(
        with_generation_suffix("degraded/audit_bridge", generation));
  }

  if (signals.consecutive_patch_failures > 0U) {
    return make_detail_ref(
        with_generation_suffix("degraded/patch_failures", generation));
  }

  return make_detail_ref(with_generation_suffix("degraded/state", generation));
}

ProbeResult PolicyHealthProbe::make_failure_result(
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
      .detail_ref = detail_ref.empty()
                        ? make_detail_ref(std::string("failure"))
                        : std::move(detail_ref),
      .timestamp = timestamp_ms > 0 ? timestamp_ms : current_time_unix_ms(),
  };
}

ProbeResult PolicyHealthProbe::probe() {
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

  if (sample.state == PolicyHealthSampleState::Timeout) {
    return make_failure_result(
        contracts::ResultCode::ProviderTimeout,
        ProbeStatus::Degraded,
        sample.latency_ms > 0 ? sample.latency_ms : descriptor_.timeout_ms,
        sample.sampled_at_unix_ms,
        sample.detail_ref.empty() ? make_detail_ref("timeout")
                                  : sample.detail_ref);
  }

  if (sample.state == PolicyHealthSampleState::Invalid) {
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

}  // namespace dasall::infra::policy