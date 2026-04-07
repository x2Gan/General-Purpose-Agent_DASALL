#include "ota/OTAHealthProbe.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <string>
#include <string_view>
#include <utility>

namespace dasall::infra::ota {
namespace {

[[nodiscard]] std::int64_t current_time_unix_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

[[nodiscard]] std::string make_detail_ref(std::string suffix) {
  return std::string(kOTAHealthDetailNamespace) + "/" + std::move(suffix);
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

OTAHealthProbe::OTAHealthProbe(
    std::shared_ptr<IOTAHealthSignalProvider> signal_provider)
    : signal_provider_(std::move(signal_provider)) {}

ProbeDescriptor OTAHealthProbe::make_descriptor() {
  return ProbeDescriptor{
      .probe_name = std::string(kOTAHealthProbeName),
      .group = std::string(kOTAHealthProbeGroup),
      .criticality = ProbeCriticality::Critical,
      .interval_ms = kOTAHealthProbeIntervalMs,
      .timeout_ms = kOTAHealthProbeTimeoutMs,
  };
}

ProbeStatus OTAHealthProbe::map_status(const OTAHealthSignals& signals) {
  if (signals.rollback_degraded || signals.audit_bridge_degraded ||
      signals.status_snapshot.pending_confirm ||
      signals.status_snapshot.backlog_count > 0 ||
      signals.status_snapshot.last_failure_code.has_value()) {
    return ProbeStatus::Degraded;
  }

  return ProbeStatus::Healthy;
}

std::string OTAHealthProbe::detail_ref_for_state(const OTAHealthSignals& signals,
                                                 ProbeStatus status) {
  if (status == ProbeStatus::Healthy) {
    return make_detail_ref("ready/slot/" +
                           sanitize_detail_segment(signals.status_snapshot.active_slot));
  }

  if (signals.rollback_degraded) {
    return signals.last_detail_ref.empty() ? make_detail_ref("degraded/rollback")
                                           : signals.last_detail_ref;
  }

  if (signals.audit_bridge_degraded) {
    return signals.last_detail_ref.empty() ? make_detail_ref("degraded/audit_bridge")
                                           : signals.last_detail_ref;
  }

  if (signals.status_snapshot.last_failure_code.has_value()) {
    return make_detail_ref(
        std::string("degraded/last_failure/") +
        std::string(dasall::contracts::result_code_category_name(
            dasall::contracts::classify_result_code(
                *signals.status_snapshot.last_failure_code))));
  }

  if (signals.status_snapshot.pending_confirm) {
    return make_detail_ref(
        std::string("degraded/pending_confirm/count/") +
        std::to_string(signals.pending_confirm_count()) + "/backlog/" +
        std::to_string(signals.status_snapshot.backlog_count));
  }

  if (signals.status_snapshot.backlog_count > 0U) {
    return make_detail_ref("degraded/backlog/" +
                           std::to_string(signals.status_snapshot.backlog_count));
  }

  return make_detail_ref("degraded/state/" +
                         sanitize_detail_segment(signals.status_snapshot.state));
}

ProbeResult OTAHealthProbe::make_failure_result(contracts::ResultCode error_code,
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

ProbeResult OTAHealthProbe::probe() {
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

  if (sample.state == OTAHealthSampleState::Timeout) {
    return make_failure_result(
        contracts::ResultCode::ProviderTimeout,
        ProbeStatus::Degraded,
        sample.latency_ms > 0 ? sample.latency_ms : descriptor_.timeout_ms,
        sample.sampled_at_unix_ms,
        sample.detail_ref.empty() ? make_detail_ref("timeout")
                                  : sample.detail_ref);
  }

  if (sample.state == OTAHealthSampleState::Invalid) {
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

}  // namespace dasall::infra::ota