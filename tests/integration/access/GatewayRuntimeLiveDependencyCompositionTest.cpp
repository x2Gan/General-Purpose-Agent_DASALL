#include <algorithm>
#include <exception>
#include <iostream>
#include <memory>
#include <string>

#include "AgentFacade.h"
#include "RuntimeDependencySet.h"
#include "RuntimeLiveDependencyComposition.h"
#include "ProfileCatalog.h"
#include "RuntimePolicyProvider.h"
#include "config/InstallLayout.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::support::assert_true;

constexpr char kDefaultProfileId[] = "desktop_full";

[[nodiscard]] std::shared_ptr<const dasall::profiles::RuntimePolicySnapshot>
load_runtime_policy_snapshot() {
  const auto install_layout = dasall::infra::config::resolve_install_layout();
  const dasall::profiles::ProfileCatalog catalog(install_layout.profiles_root);
  const dasall::profiles::RuntimePolicyProvider provider(catalog);
  const auto snapshot_result = provider.load_snapshot(
      dasall::profiles::RuntimePolicyLoadRequest{
          .profile_id = kDefaultProfileId,
      });
  assert_true(snapshot_result.ok() && snapshot_result.snapshot != nullptr,
              "gateway runtime live dependency composition should load a runtime policy snapshot");
  return snapshot_result.snapshot;
}

[[nodiscard]] bool contains_port(const std::vector<std::string>& ports,
                                 const std::string& expected_port) {
  return std::find(ports.begin(), ports.end(), expected_port) != ports.end();
}

void gateway_runtime_live_dependency_composition_establishes_degraded_baseline() {
  const auto policy_snapshot = load_runtime_policy_snapshot();
  const auto composition =
      dasall::apps::runtime_support::compose_minimal_live_dependency_set(
          policy_snapshot,
          "gateway.http-unary");
  assert_true(composition.ok(),
              "gateway runtime live dependency composition should materialize required ports: " +
                  composition.error);

  const auto readiness = composition.dependency_set->describe_readiness();
  assert_true(readiness.has_required_ports,
              "gateway runtime live dependency composition should provide all required ports: " +
                  readiness.summary());
  assert_true(readiness.degraded,
              "gateway runtime live dependency composition should remain degraded while optional ports are missing: " +
                  readiness.summary());
  assert_true(readiness.missing_required_ports.empty(),
              "gateway runtime live dependency composition should not leave required ports empty");
    assert_true(contains_port(readiness.missing_optional_ports, "knowledge") &&
            !contains_port(readiness.missing_optional_ports, "llm"),
          "gateway runtime live dependency composition should inject llm while naming missing knowledge: " +
                  readiness.summary());
    assert_true(composition.dependency_set->llm_manager != nullptr,
          "gateway runtime live dependency composition should expose a production ILLMManager");

  dasall::runtime::AgentInitRequest init_request;
  init_request.runtime_instance_id =
      "gateway.http-unary:" + policy_snapshot->effective_profile_id();
  init_request.profile_id = policy_snapshot->effective_profile_id();
  init_request.policy_snapshot = policy_snapshot;
  init_request.dependency_set = composition.dependency_set;
  init_request.boot_reason = "gateway-http-entry";
  init_request.cold_start = true;

  dasall::runtime::AgentFacade facade;
  const auto init_result = facade.init(init_request);
  assert_true(init_result.accepted,
              "gateway runtime live dependency composition should keep init accepted: " +
                  init_result.health_summary + " (" + init_result.diagnostics + ")");
  assert_true(init_result.degraded_ready(),
              "gateway runtime live dependency composition should surface degraded-ready entrypoint: " +
                  init_result.diagnostics);
  assert_true(!init_result.stub_ready(),
              "gateway runtime live dependency composition should not fall back to stub-ready: " +
                  init_result.diagnostics);
  assert_true(!init_result.default_ready(),
          "gateway runtime live dependency composition should not claim default-ready while knowledge remains absent: " +
                  init_result.diagnostics);
}

}  // namespace

int main() {
  try {
    gateway_runtime_live_dependency_composition_establishes_degraded_baseline();
  } catch (const std::exception& ex) {
    std::cerr << "[GatewayRuntimeLiveDependencyCompositionTest] FAILED: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}