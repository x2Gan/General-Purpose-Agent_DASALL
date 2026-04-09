#include "ops/ServiceHealthProbe.h"

#include <algorithm>
#include <chrono>
#include <string_view>
#include <utility>

namespace dasall::services::internal {

namespace {

[[nodiscard]] std::int64_t current_time_unix_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

void append_component(infra::HealthSnapshot::ComponentList* components,
                      std::string_view component) {
  if (component.empty()) {
    return;
  }

  const auto existing = std::find(components->begin(),
                                  components->end(),
                                  std::string(component));
  if (existing == components->end()) {
    components->push_back(std::string(component));
  }
}

[[nodiscard]] bool has_component(const infra::HealthSnapshot& snapshot,
                                 std::string_view component) {
  return std::find(snapshot.failed_components.begin(),
                   snapshot.failed_components.end(),
                   std::string(component)) != snapshot.failed_components.end();
}

}  // namespace

ServiceHealthProbe::ServiceHealthProbe(
    std::shared_ptr<IServiceHealthSignalProvider> signal_provider,
    ServiceHealthProbeOptions options)
    : signal_provider_(std::move(signal_provider)), options_(std::move(options)) {
  if (options_.detail_namespace.empty()) {
    options_.detail_namespace = std::string(kServiceHealthDetailNamespace);
  }
}

infra::ProbeDescriptor ServiceHealthProbe::make_descriptor() {
  return infra::ProbeDescriptor{
      .probe_name = std::string(kServiceHealthProbeName),
      .group = std::string(kServiceHealthProbeGroup),
      .criticality = infra::ProbeCriticality::Critical,
      .interval_ms = kServiceHealthProbeIntervalMs,
      .timeout_ms = kServiceHealthProbeTimeoutMs,
  };
}

infra::HealthSnapshot ServiceHealthProbe::build_snapshot(
    const ServiceHealthSample& sample,
    std::uint64_t version) {
  infra::HealthSnapshot snapshot{
      .liveness = sample.system_snapshot_ready,
      .readiness = sample.system_snapshot_ready,
      .degraded = false,
      .failed_components = {},
      .version = version,
      .timestamp = sample.sampled_at_unix_ms,
  };

  bool readiness_blocked = false;
  bool degraded = false;

  if (!sample.system_snapshot_ready) {
    append_component(&snapshot.failed_components, "services.system_snapshot");
    snapshot.readiness = false;
    snapshot.degraded = false;
    return snapshot;
  }

  switch (sample.circuit_state) {
    case ServiceCircuitState::open:
    case ServiceCircuitState::unknown:
      readiness_blocked = true;
      degraded = true;
      append_component(&snapshot.failed_components, "services.circuit");
      break;
    case ServiceCircuitState::half_open:
      degraded = true;
      append_component(&snapshot.failed_components, "services.circuit");
      break;
    case ServiceCircuitState::closed:
      break;
  }

  switch (sample.adapter_readiness) {
    case AdapterAvailabilityState::unavailable:
    case AdapterAvailabilityState::unknown:
      readiness_blocked = true;
      degraded = true;
      append_component(&snapshot.failed_components, "services.adapter");
      break;
    case AdapterAvailabilityState::degraded:
      degraded = true;
      append_component(&snapshot.failed_components, "services.adapter");
      break;
    case AdapterAvailabilityState::available:
      break;
  }

  if (sample.command_queue.blocks_readiness()) {
    readiness_blocked = true;
    degraded = true;
    append_component(&snapshot.failed_components, "services.command_queue");
  }

  if (sample.subscription_queue.blocks_readiness()) {
    readiness_blocked = true;
    degraded = true;
    append_component(&snapshot.failed_components, "services.subscription_queue");
  }

  if (sample.system_snapshot_degraded) {
    degraded = true;
    append_component(&snapshot.failed_components, "services.system_snapshot");
  }

  if (sample.audit_bridge_degraded) {
    degraded = true;
    append_component(&snapshot.failed_components, "services.audit_bridge");
  }

  if (sample.metrics_bridge_degraded) {
    degraded = true;
    append_component(&snapshot.failed_components, "services.metrics_bridge");
  }

  if (sample.trace_bridge_degraded) {
    degraded = true;
    append_component(&snapshot.failed_components, "services.trace_bridge");
  }

  snapshot.readiness = snapshot.readiness && !readiness_blocked;
  snapshot.degraded = snapshot.liveness && degraded;
  return snapshot;
}

infra::ProbeStatus ServiceHealthProbe::probe_status_for(
    const infra::HealthSnapshot& snapshot) {
  if (!snapshot.has_consistent_state()) {
    return infra::ProbeStatus::Unknown;
  }

  if (!snapshot.liveness) {
    return infra::ProbeStatus::Unhealthy;
  }

  if (!snapshot.readiness || snapshot.degraded) {
    return infra::ProbeStatus::Degraded;
  }

  return infra::ProbeStatus::Healthy;
}

std::string ServiceHealthProbe::detail_ref_for(
    const ServiceHealthSample& sample,
    const infra::HealthSnapshot& snapshot) const {
  if (!snapshot.liveness) {
    return make_detail_ref("unhealthy/system_snapshot");
  }

  if (has_component(snapshot, "services.circuit")) {
    switch (sample.circuit_state) {
      case ServiceCircuitState::open:
        return make_detail_ref("degraded/circuit_open");
      case ServiceCircuitState::half_open:
        return make_detail_ref("degraded/circuit_half_open");
      case ServiceCircuitState::unknown:
        return make_detail_ref("degraded/circuit_unknown");
      case ServiceCircuitState::closed:
        break;
    }
  }

  if (has_component(snapshot, "services.adapter")) {
    switch (sample.adapter_readiness) {
      case AdapterAvailabilityState::unavailable:
        return make_detail_ref("degraded/adapter_unavailable");
      case AdapterAvailabilityState::unknown:
        return make_detail_ref("degraded/adapter_unknown");
      case AdapterAvailabilityState::degraded:
        return make_detail_ref("degraded/adapter_degraded");
      case AdapterAvailabilityState::available:
        break;
    }
  }

  if (has_component(snapshot, "services.command_queue")) {
    return make_detail_ref("degraded/command_queue");
  }

  if (has_component(snapshot, "services.subscription_queue")) {
    return make_detail_ref("degraded/subscription_queue");
  }

  if (has_component(snapshot, "services.metrics_bridge")) {
    return make_detail_ref("degraded/metrics_bridge");
  }

  if (has_component(snapshot, "services.trace_bridge")) {
    return make_detail_ref("degraded/trace_bridge");
  }

  if (has_component(snapshot, "services.audit_bridge")) {
    return make_detail_ref("degraded/audit_bridge");
  }

  if (has_component(snapshot, "services.system_snapshot")) {
    return make_detail_ref("degraded/system_snapshot");
  }

  if (!sample.detail_ref.empty()) {
    return sample.detail_ref;
  }

  return make_detail_ref("healthy");
}

infra::ProbeResult ServiceHealthProbe::make_failure_result(
    contracts::ResultCode error_code,
    infra::ProbeStatus status,
    std::int64_t latency_ms,
    std::int64_t timestamp_ms,
    std::string detail_ref,
    std::vector<std::string> failed_components) {
  if (failed_components.empty()) {
    failed_components.push_back("services.health_probe");
  }

  latest_snapshot_ = infra::HealthSnapshot{
      .liveness = false,
      .readiness = false,
      .degraded = false,
      .failed_components = std::move(failed_components),
      .version = next_snapshot_version_++,
      .timestamp = timestamp_ms,
  };

  return infra::ProbeResult{
      .probe_name = descriptor_.probe_name,
      .status = status,
      .latency_ms = std::max<std::int64_t>(0, latency_ms),
      .error_code = error_code,
      .detail_ref = detail_ref.empty() ? make_detail_ref("failure")
                                       : std::move(detail_ref),
      .timestamp = timestamp_ms,
  };
}

std::int64_t ServiceHealthProbe::current_time_unix_ms() const {
  if (options_.now_ms) {
    return options_.now_ms();
  }

  return current_time_unix_ms();
}

std::string ServiceHealthProbe::make_detail_ref(std::string_view suffix) const {
  return options_.detail_namespace + "/" + std::string(suffix);
}

infra::ProbeResult ServiceHealthProbe::probe() {
  if (!signal_provider_) {
    return make_failure_result(
        contracts::ResultCode::ValidationFieldMissing,
        infra::ProbeStatus::Unknown,
        0,
        current_time_unix_ms(),
        make_detail_ref("config/provider_missing"),
        {"services.health_probe"});
  }

  const auto sample = signal_provider_->sample(descriptor_.timeout_ms);
  if (!sample.has_consistent_values()) {
    const auto timestamp_ms =
        sample.sampled_at_unix_ms > 0 ? sample.sampled_at_unix_ms
                                      : current_time_unix_ms();
    return make_failure_result(
        contracts::ResultCode::ValidationFieldMissing,
        infra::ProbeStatus::Unknown,
        sample.latency_ms,
        timestamp_ms,
        sample.detail_ref.empty() ? make_detail_ref("invalid/sample")
                                  : sample.detail_ref,
        {"services.health_probe"});
  }

  auto snapshot = build_snapshot(sample, next_snapshot_version_++);
  latest_snapshot_ = snapshot;
  const auto status = probe_status_for(snapshot);

  return infra::ProbeResult{
      .probe_name = descriptor_.probe_name,
      .status = status,
      .latency_ms = sample.latency_ms,
      .error_code = std::nullopt,
      .detail_ref = detail_ref_for(sample, snapshot),
      .timestamp = sample.sampled_at_unix_ms,
  };
}

infra::HealthSnapshot ServiceHealthProbe::snapshot() const {
  if (latest_snapshot_.has_value()) {
    return *latest_snapshot_;
  }

  return infra::HealthSnapshot{
      .liveness = false,
      .readiness = false,
      .degraded = false,
      .failed_components = {"services.health_probe"},
      .version = 0U,
      .timestamp = 0,
  };
}

}  // namespace dasall::services::internal