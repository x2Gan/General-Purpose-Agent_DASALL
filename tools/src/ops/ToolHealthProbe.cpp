#include "ops/ToolHealthProbe.h"

#include <algorithm>
#include <chrono>
#include <string_view>
#include <utility>

namespace dasall::tools::ops {

namespace {

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

ToolHealthProbe::ToolHealthProbe(
    std::shared_ptr<IToolHealthSignalProvider> signal_provider,
    ToolHealthProbeOptions options)
    : signal_provider_(std::move(signal_provider)), options_(std::move(options)) {
  if (options_.detail_namespace.empty()) {
    options_.detail_namespace = std::string(kToolHealthDetailNamespace);
  }
}

infra::ProbeDescriptor ToolHealthProbe::make_descriptor() {
  return infra::ProbeDescriptor{
      .probe_name = std::string(kToolHealthProbeName),
      .group = std::string(kToolHealthProbeGroup),
      .criticality = infra::ProbeCriticality::Critical,
      .interval_ms = kToolHealthProbeIntervalMs,
      .timeout_ms = kToolHealthProbeTimeoutMs,
  };
}

void ToolHealthProbe::collect_registry_health(const ToolHealthSample& sample,
                                              infra::HealthSnapshot* snapshot) {
  if (!sample.registry.descriptor_catalog_ready || sample.registry.revision == 0U) {
    append_component(&snapshot->failed_components, "tools.registry");
    snapshot->liveness = false;
    snapshot->readiness = false;
    snapshot->degraded = false;
    return;
  }

  if (sample.registry.delta_pipeline_degraded) {
    append_component(&snapshot->failed_components, "tools.registry_delta");
    snapshot->degraded = true;
  }
}

void ToolHealthProbe::collect_lane_health(
    const ToolHealthSample& sample,
    infra::HealthSnapshot* snapshot,
    route::ToolRouteHealthSnapshot* route_health) {
  route_health->builtin_lane_healthy = !sample.builtin_lane.blocks_route();
  route_health->workflow_lane_healthy = !sample.workflow_lane.blocks_route();

  if (sample.builtin_lane.blocks_route()) {
    append_component(&snapshot->failed_components, "tools.builtin_lane");
    snapshot->readiness = false;
    snapshot->degraded = true;
  }

  if (sample.workflow_lane.blocks_route()) {
    append_component(&snapshot->failed_components, "tools.workflow_lane");
    snapshot->degraded = true;
  }
}

void ToolHealthProbe::collect_mcp_health(const ToolHealthSample& sample,
                                         infra::HealthSnapshot* snapshot,
                                         route::ToolRouteHealthSnapshot* route_health) {
  route_health->mcp_lane_healthy = !sample.mcp.blocks_route();

  if (!sample.mcp.session_ready) {
    append_component(&snapshot->failed_components, "tools.mcp_session");
    snapshot->degraded = true;
  }

  if (sample.mcp.freshness == CapabilityFreshness::stale) {
    append_component(&snapshot->failed_components, "tools.capability_cache");
    snapshot->degraded = true;
  }

  if (sample.mcp.freshness == CapabilityFreshness::expired) {
    append_component(&snapshot->failed_components, "tools.capability_cache");
    snapshot->degraded = true;
  }

  if (sample.mcp.last_error.has_value()) {
    append_component(&snapshot->failed_components, "tools.capability_cache");
    snapshot->degraded = true;
  }
}

infra::HealthSnapshot ToolHealthProbe::build_snapshot(
    const ToolHealthSample& sample,
    std::uint64_t version) {
  infra::HealthSnapshot snapshot{
      .liveness = true,
      .readiness = true,
      .degraded = false,
      .failed_components = {},
      .version = version,
      .timestamp = sample.sampled_at_unix_ms,
  };
  route::ToolRouteHealthSnapshot route_health{};

  collect_registry_health(sample, &snapshot);
  if (!snapshot.liveness) {
    route_health = route::ToolRouteHealthSnapshot{
        .builtin_lane_healthy = false,
        .workflow_lane_healthy = false,
        .mcp_lane_healthy = false,
    };
    return snapshot;
  }

  collect_lane_health(sample, &snapshot, &route_health);
  collect_mcp_health(sample, &snapshot, &route_health);

  if (sample.audit_bridge_degraded) {
    append_component(&snapshot.failed_components, "tools.audit_bridge");
    snapshot.degraded = true;
  }

  if (sample.metrics_bridge_degraded) {
    append_component(&snapshot.failed_components, "tools.metrics_bridge");
    snapshot.degraded = true;
  }

  if (sample.trace_bridge_degraded) {
    append_component(&snapshot.failed_components, "tools.trace_bridge");
    snapshot.degraded = true;
  }

  if (!snapshot.readiness) {
    snapshot.degraded = true;
  }

  return snapshot;
}

route::ToolRouteHealthSnapshot ToolHealthProbe::build_route_health(
    const ToolHealthSample& sample) {
  route::ToolRouteHealthSnapshot route_health{};
  if (!sample.registry.descriptor_catalog_ready || sample.registry.revision == 0U) {
    route_health.builtin_lane_healthy = false;
    route_health.workflow_lane_healthy = false;
    route_health.mcp_lane_healthy = false;
    return route_health;
  }

  route_health.builtin_lane_healthy = !sample.builtin_lane.blocks_route();
  route_health.workflow_lane_healthy = !sample.workflow_lane.blocks_route();
  route_health.mcp_lane_healthy = !sample.mcp.blocks_route();
  return route_health;
}

infra::ProbeStatus ToolHealthProbe::probe_status_for(
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

std::string ToolHealthProbe::detail_ref_for(
    const ToolHealthSample& sample,
    const infra::HealthSnapshot& snapshot) const {
  if (!snapshot.liveness) {
    return make_detail_ref("unhealthy/registry");
  }

  if (has_component(snapshot, "tools.builtin_lane")) {
    return make_detail_ref("degraded/builtin_lane");
  }

  if (has_component(snapshot, "tools.workflow_lane")) {
    return make_detail_ref("degraded/workflow_lane");
  }

  if (has_component(snapshot, "tools.mcp_session")) {
    return make_detail_ref("degraded/mcp_session");
  }

  if (has_component(snapshot, "tools.capability_cache")) {
    switch (sample.mcp.freshness) {
      case CapabilityFreshness::stale:
        return make_detail_ref("degraded/capability_cache_stale");
      case CapabilityFreshness::expired:
        return make_detail_ref("degraded/capability_cache_expired");
      case CapabilityFreshness::fresh:
        break;
    }
    return make_detail_ref("degraded/capability_cache");
  }

  if (has_component(snapshot, "tools.trace_bridge")) {
    return make_detail_ref("degraded/trace_bridge");
  }

  if (has_component(snapshot, "tools.metrics_bridge")) {
    return make_detail_ref("degraded/metrics_bridge");
  }

  if (has_component(snapshot, "tools.audit_bridge")) {
    return make_detail_ref("degraded/audit_bridge");
  }

  if (has_component(snapshot, "tools.registry_delta")) {
    return make_detail_ref("degraded/registry_delta");
  }

  if (!sample.detail_ref.empty()) {
    return sample.detail_ref;
  }

  return make_detail_ref("healthy");
}

infra::ProbeResult ToolHealthProbe::make_failure_result(
    contracts::ResultCode error_code,
    infra::ProbeStatus status,
    std::int64_t latency_ms,
    std::int64_t timestamp_ms,
    std::string detail_ref,
    std::vector<std::string> failed_components) {
  if (failed_components.empty()) {
    failed_components.push_back("tools.health_probe");
  }

  latest_snapshot_ = infra::HealthSnapshot{
      .liveness = false,
      .readiness = false,
      .degraded = false,
      .failed_components = std::move(failed_components),
      .version = next_snapshot_version_++,
      .timestamp = timestamp_ms,
  };
  latest_route_health_ = route::ToolRouteHealthSnapshot{
      .builtin_lane_healthy = false,
      .workflow_lane_healthy = false,
      .mcp_lane_healthy = false,
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

std::int64_t ToolHealthProbe::current_time_unix_ms() const {
  if (options_.now_ms) {
    return options_.now_ms();
  }

  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

std::string ToolHealthProbe::make_detail_ref(std::string_view suffix) const {
  return options_.detail_namespace + "/" + std::string(suffix);
}

infra::ProbeResult ToolHealthProbe::probe() {
  if (!signal_provider_) {
    return make_failure_result(
        contracts::ResultCode::ValidationFieldMissing,
        infra::ProbeStatus::Unknown,
        0,
        current_time_unix_ms(),
        make_detail_ref("config/provider_missing"),
        {"tools.health_probe"});
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
        {"tools.health_probe"});
  }

  latest_snapshot_ = build_snapshot(sample, next_snapshot_version_++);
  latest_route_health_ = build_route_health(sample);
  const auto status = probe_status_for(*latest_snapshot_);

  return infra::ProbeResult{
      .probe_name = descriptor_.probe_name,
      .status = status,
      .latency_ms = sample.latency_ms,
      .error_code = std::nullopt,
      .detail_ref = detail_ref_for(sample, *latest_snapshot_),
      .timestamp = sample.sampled_at_unix_ms,
  };
}

infra::HealthSnapshot ToolHealthProbe::snapshot() const {
  if (latest_snapshot_.has_value()) {
    return *latest_snapshot_;
  }

  return infra::HealthSnapshot{
      .liveness = false,
      .readiness = false,
      .degraded = false,
      .failed_components = {"tools.health_probe"},
      .version = 0U,
      .timestamp = 0,
  };
}

route::ToolRouteHealthSnapshot ToolHealthProbe::route_health_snapshot() const {
  return latest_route_health_;
}

}  // namespace dasall::tools::ops