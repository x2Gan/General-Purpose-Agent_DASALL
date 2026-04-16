#include <algorithm>
#include <exception>
#include <iostream>
#include <memory>
#include <string>

#include "ops/ToolHealthProbe.h"
#include "support/TestAssertions.h"

namespace {

using dasall::infra::ProbeStatus;
using dasall::tests::support::assert_true;
using dasall::tools::CapabilityFreshness;
using dasall::tools::ops::IToolHealthSignalProvider;
using dasall::tools::ops::ToolHealthProbe;
using dasall::tools::ops::ToolHealthSample;

class StaticSignalProvider final : public IToolHealthSignalProvider {
 public:
  explicit StaticSignalProvider(ToolHealthSample sample)
      : sample_(std::move(sample)) {}

  [[nodiscard]] ToolHealthSample sample(std::int64_t) override {
    return sample_;
  }

 private:
  ToolHealthSample sample_;
};

[[nodiscard]] ToolHealthSample make_sample() {
  ToolHealthSample sample;
  sample.registry.revision = 7U;
  sample.latency_ms = 8;
  sample.sampled_at_unix_ms = 1712750400000LL;
  sample.detail_ref = "status://tools/health/test";
  return sample;
}

[[nodiscard]] bool has_component(const dasall::infra::HealthSnapshot& snapshot,
                                 const std::string& component) {
  return std::find(snapshot.failed_components.begin(),
                   snapshot.failed_components.end(),
                   component) != snapshot.failed_components.end();
}

void test_tool_health_probe_marks_registry_missing_as_unhealthy() {
  auto sample = make_sample();
  sample.registry.revision = 0U;
  sample.registry.descriptor_catalog_ready = false;

  ToolHealthProbe probe(std::make_shared<StaticSignalProvider>(sample));
  const auto result = probe.probe();
  const auto snapshot = probe.snapshot();
  const auto route_health = probe.route_health_snapshot();

  assert_true(result.has_consistent_state() &&
                  result.status == ProbeStatus::Unhealthy &&
                  result.detail_ref.find("unhealthy/registry") != std::string::npos,
              "missing registry revision should surface as an unhealthy tools probe result");
  assert_true(!snapshot.liveness && !snapshot.readiness &&
                  has_component(snapshot, "tools.registry"),
              "missing registry should force tools liveness/readiness false and expose the registry component");
  assert_true(!route_health.builtin_lane_healthy &&
                  !route_health.workflow_lane_healthy &&
                  !route_health.mcp_lane_healthy,
              "missing registry should disable all route health switches");
}

void test_tool_health_probe_marks_builtin_lane_saturation_as_not_ready() {
  auto sample = make_sample();
  sample.builtin_lane.saturated = true;
  sample.builtin_lane.concurrency_budget = 1U;

  ToolHealthProbe probe(std::make_shared<StaticSignalProvider>(sample));
  const auto result = probe.probe();
  const auto snapshot = probe.snapshot();
  const auto route_health = probe.route_health_snapshot();

  assert_true(result.status == ProbeStatus::Degraded &&
                  result.detail_ref.find("builtin_lane") != std::string::npos,
              "builtin lane saturation should surface as a degraded tools readiness result");
  assert_true(snapshot.liveness && !snapshot.readiness && snapshot.degraded &&
                  has_component(snapshot, "tools.builtin_lane"),
              "builtin lane saturation should keep tools live but block readiness and expose the builtin lane component");
  assert_true(!route_health.builtin_lane_healthy &&
                  route_health.workflow_lane_healthy &&
                  route_health.mcp_lane_healthy,
              "builtin lane saturation should only trip the builtin route health switch");
}

void test_tool_health_probe_marks_stale_cache_and_trace_bridge_as_degraded() {
  auto sample = make_sample();
  sample.mcp.freshness = CapabilityFreshness::stale;
  sample.mcp.stale_read_allowed = true;
  sample.mcp.last_error = std::string("refresh_timeout");
  sample.trace_bridge_degraded = true;

  ToolHealthProbe probe(std::make_shared<StaticSignalProvider>(sample));
  const auto result = probe.probe();
  const auto snapshot = probe.snapshot();
  const auto route_health = probe.route_health_snapshot();

  assert_true(result.status == ProbeStatus::Degraded &&
                  result.detail_ref.find("capability_cache_stale") != std::string::npos,
              "stale capability cache should surface as a degraded tools result when stale reads remain allowed");
  assert_true(snapshot.liveness && snapshot.readiness && snapshot.degraded &&
                  has_component(snapshot, "tools.capability_cache") &&
                  has_component(snapshot, "tools.trace_bridge"),
              "stale cache and trace bridge degradation should preserve readiness while marking the corresponding failed components");
  assert_true(route_health.builtin_lane_healthy &&
                  route_health.workflow_lane_healthy &&
                  route_health.mcp_lane_healthy,
              "stale-but-allowed capability cache should keep the mcp route health switch available");
}

void test_tool_health_probe_returns_unknown_when_provider_is_missing() {
  ToolHealthProbe probe(nullptr);
  const auto result = probe.probe();
  const auto snapshot = probe.snapshot();

  assert_true(result.has_consistent_state() &&
                  result.status == ProbeStatus::Unknown,
              "missing health signal provider should surface as an unknown tools probe result");
  assert_true(!snapshot.liveness && !snapshot.readiness &&
                  has_component(snapshot, "tools.health_probe"),
              "missing health signal provider should update the cached tools snapshot to an unhealthy probe state");
}

}  // namespace

int main() {
  try {
    test_tool_health_probe_marks_registry_missing_as_unhealthy();
    test_tool_health_probe_marks_builtin_lane_saturation_as_not_ready();
    test_tool_health_probe_marks_stale_cache_and_trace_bridge_as_degraded();
    test_tool_health_probe_returns_unknown_when_provider_is_missing();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}