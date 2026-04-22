#include "RuntimeHealthProbe.h"

#include <algorithm>
#include <chrono>

namespace dasall::runtime {
namespace {

void append_unique_component(infra::HealthSnapshot::ComponentList* components,
                             std::string component) {
  if (component.empty()) {
    return;
  }

  const auto existing = std::find(components->begin(), components->end(), component);
  if (existing == components->end()) {
    components->push_back(std::move(component));
  }
}

[[nodiscard]] bool has_component(const infra::HealthSnapshot& snapshot,
                                 const std::string& component) {
  return std::find(snapshot.failed_components.begin(),
                   snapshot.failed_components.end(),
                   component) != snapshot.failed_components.end();
}

}  // namespace

bool RuntimeHealthSample::has_consistent_values() const {
  if (latency_ms < 0 || sampled_at_unix_ms <= 0) {
    return false;
  }

  for (std::size_t index = 0U; index < failed_components.size(); ++index) {
    if (failed_components[index].empty()) {
      return false;
    }

    if (std::find(failed_components.begin() + static_cast<std::ptrdiff_t>(index + 1U),
                  failed_components.end(),
                  failed_components[index]) != failed_components.end()) {
      return false;
    }
  }

  return true;
}

RuntimeHealthProbe::RuntimeHealthProbe(
    std::shared_ptr<IRuntimeHealthSignalProvider> signal_provider,
    RuntimeHealthProbeOptions options)
    : signal_provider_(std::move(signal_provider)), options_(std::move(options)) {
  if (options_.detail_namespace.empty()) {
    options_.detail_namespace = std::string(kRuntimeHealthDetailNamespace);
  }

  if (!options_.now_ms) {
    options_.now_ms = [this]() { return current_time_unix_ms(); };
  }
}

infra::ProbeDescriptor RuntimeHealthProbe::make_descriptor() {
  return infra::ProbeDescriptor{
      .probe_name = std::string(kRuntimeHealthProbeName),
      .group = std::string(kRuntimeHealthProbeGroup),
      .criticality = infra::ProbeCriticality::Critical,
      .interval_ms = kRuntimeHealthProbeIntervalMs,
      .timeout_ms = kRuntimeHealthProbeTimeoutMs,
  };
}

infra::HealthSnapshot RuntimeHealthProbe::build_snapshot(
    const RuntimeHealthSample& sample,
    const std::uint64_t version) {
  infra::HealthSnapshot snapshot{
      .liveness = sample.watchdog_healthy,
      .readiness = sample.watchdog_healthy && sample.dependencies_ready,
      .degraded = false,
      .failed_components = {},
      .version = version,
      .timestamp = sample.sampled_at_unix_ms,
  };

  for (const auto& component : sample.failed_components) {
    append_unique_component(&snapshot.failed_components, component);
  }

  if (!sample.watchdog_healthy) {
    append_unique_component(&snapshot.failed_components, "runtime.watchdog");
  }

  if (!sample.dependencies_ready) {
    append_unique_component(&snapshot.failed_components, "runtime.dependencies");
    snapshot.degraded = true;
  }

  if (sample.event_bus_overflow) {
    append_unique_component(&snapshot.failed_components, "runtime.event_bus");
    snapshot.degraded = true;
  }

  if (sample.telemetry_degraded) {
    append_unique_component(&snapshot.failed_components, "runtime.telemetry");
    snapshot.degraded = true;
  }

  if (sample.maintenance_backlog) {
    append_unique_component(&snapshot.failed_components, "runtime.maintenance");
    snapshot.degraded = true;
  }

  if (sample.safe_mode_active) {
    append_unique_component(&snapshot.failed_components, "runtime.safe_mode");
    snapshot.degraded = true;
  }

  if (!snapshot.liveness) {
    snapshot.readiness = false;
    snapshot.degraded = false;
  } else if (!snapshot.readiness) {
    snapshot.degraded = true;
  }

  return snapshot;
}

infra::ProbeStatus RuntimeHealthProbe::probe_status_for(
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

RuntimeHealthSample RuntimeHealthProbe::fallback_sample(std::string component) const {
  RuntimeHealthSample sample{
      .dependencies_ready = false,
      .watchdog_healthy = false,
      .telemetry_degraded = false,
      .event_bus_overflow = false,
      .maintenance_backlog = false,
      .safe_mode_active = false,
      .failed_components = {},
      .latency_ms = 0,
      .sampled_at_unix_ms = options_.now_ms(),
      .detail_ref = make_detail_ref(component),
  };
  sample.failed_components.push_back(std::move(component));
  return sample;
}

infra::HealthSnapshot RuntimeHealthProbe::collect_snapshot() {
  RuntimeHealthSample sample = signal_provider_ != nullptr
                                   ? signal_provider_->sample(kRuntimeHealthProbeTimeoutMs)
                                   : fallback_sample("runtime.signal_provider");
  if (!sample.has_consistent_values()) {
    sample = fallback_sample("runtime.health_sample");
  }

  const auto snapshot = build_snapshot(sample, next_snapshot_version_++);
  const std::lock_guard<std::mutex> lock(snapshot_mutex_);
  latest_sample_ = sample;
  latest_snapshot_ = snapshot;
  return snapshot;
}

infra::ProbeResult RuntimeHealthProbe::probe() {
  const auto snapshot = collect_snapshot();

  RuntimeHealthSample sample;
  {
    const std::lock_guard<std::mutex> lock(snapshot_mutex_);
    sample = latest_sample_.value_or(fallback_sample("runtime.signal_provider"));
  }

  return infra::ProbeResult{
      .probe_name = descriptor_.probe_name,
      .status = probe_status_for(snapshot),
      .latency_ms = sample.latency_ms,
      .error_code = std::nullopt,
      .detail_ref = detail_ref_for(sample, snapshot),
      .timestamp = snapshot.timestamp,
  };
}

infra::HealthSnapshot RuntimeHealthProbe::snapshot() const {
  const std::lock_guard<std::mutex> lock(snapshot_mutex_);
  return latest_snapshot_.value_or(infra::HealthSnapshot{});
}

std::string RuntimeHealthProbe::detail_ref_for(
    const RuntimeHealthSample& sample,
    const infra::HealthSnapshot& snapshot) const {
  if (!sample.detail_ref.empty()) {
    return sample.detail_ref;
  }

  if (has_component(snapshot, "runtime.watchdog")) {
    return make_detail_ref("unhealthy/watchdog");
  }

  if (has_component(snapshot, "runtime.dependencies")) {
    return make_detail_ref("degraded/dependencies");
  }

  if (has_component(snapshot, "runtime.event_bus")) {
    return make_detail_ref("degraded/event_bus");
  }

  if (has_component(snapshot, "runtime.telemetry")) {
    return make_detail_ref("degraded/telemetry");
  }

  if (has_component(snapshot, "runtime.maintenance")) {
    return make_detail_ref("degraded/maintenance");
  }

  if (has_component(snapshot, "runtime.safe_mode")) {
    return make_detail_ref("degraded/safe_mode");
  }

  return make_detail_ref("healthy");
}

std::int64_t RuntimeHealthProbe::current_time_unix_ms() const {
  using Clock = std::chrono::system_clock;
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             Clock::now().time_since_epoch())
      .count();
}

std::string RuntimeHealthProbe::make_detail_ref(std::string_view suffix) const {
  return options_.detail_namespace + "/" + std::string(suffix);
}

}  // namespace dasall::runtime