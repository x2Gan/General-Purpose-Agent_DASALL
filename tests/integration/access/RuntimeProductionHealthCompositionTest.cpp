#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <system_error>

#include "RuntimeDependencySet.h"
#include "RuntimeLiveDependencyComposition.h"
#include "ProfileCatalog.h"
#include "RuntimePolicyProvider.h"
#include "config/InstallLayout.h"
#include "health/IHealthMonitor.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::support::assert_true;

constexpr char kDefaultProfileId[] = "desktop_full";

class TempStateRoot {
 public:
  explicit TempStateRoot(const std::string& stem)
      : path_(std::filesystem::temp_directory_path() /
              (stem + "-" + std::to_string(std::chrono::duration_cast<std::chrono::microseconds>(
                  std::chrono::system_clock::now().time_since_epoch()).count()))) {
    std::filesystem::create_directories(path_);
  }

  ~TempStateRoot() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }

  [[nodiscard]] const std::filesystem::path& path() const {
    return path_;
  }

 private:
  std::filesystem::path path_;
};

[[nodiscard]] std::shared_ptr<const dasall::profiles::RuntimePolicySnapshot>
load_runtime_policy_snapshot() {
  const auto install_layout = dasall::infra::config::resolve_install_layout();
  const dasall::profiles::ProfileCatalog catalog(install_layout.profiles_root);
  const dasall::profiles::RuntimePolicyProvider provider(catalog);
  const auto snapshot_result = provider.load_snapshot(
      dasall::profiles::RuntimePolicyLoadRequest{.profile_id = kDefaultProfileId});
  assert_true(snapshot_result.ok() && snapshot_result.snapshot != nullptr,
              "runtime production health composition should load a runtime policy snapshot");
  return snapshot_result.snapshot;
}

[[nodiscard]] bool contains_port(const std::vector<std::string>& ports,
                                 const std::string& expected_port) {
  return std::find(ports.begin(), ports.end(), expected_port) != ports.end();
}

void runtime_production_health_composition_registers_ready_probes() {
  const auto policy_snapshot = load_runtime_policy_snapshot();
  const TempStateRoot state_root("dasall-runtime-health-composition");
  const auto composition =
      dasall::apps::runtime_support::compose_minimal_live_dependency_set(
          policy_snapshot,
          "daemon.local-control-plane",
          dasall::apps::runtime_support::RuntimeLiveDependencyCompositionOptions{
              .readonly_assets_root_override = std::filesystem::path(DASALL_SOURCE_ROOT),
              .state_root_override = state_root.path(),
          });
  assert_true(composition.ok(),
              "runtime production health composition should materialize live dependencies: " +
                  composition.error);
  assert_true(composition.dependency_set->audit_logger != nullptr &&
                  composition.dependency_set->metrics_provider != nullptr &&
                  composition.dependency_set->tracer_provider != nullptr,
              "runtime production health composition should retain shared audit, metrics, and trace providers");
  assert_true(composition.dependency_set->health_monitor != nullptr,
              "runtime production health composition should expose a concrete health monitor");
  assert_true(composition.dependency_set->health_probes.size() == 2U,
              "runtime production health composition should retain both tool and services probes");
  assert_true(contains_port(composition.dependency_set->external_evidence,
                  "runtime:daemon.local-control-plane:production-observability-health"),
              "runtime production health composition should record the observability and health evidence marker");

  const auto health_result = composition.dependency_set->health_monitor->evaluate_now();
  assert_true(health_result.ok && health_result.snapshot.readiness &&
                  !health_result.snapshot.degraded &&
                  health_result.snapshot.failed_components.empty(),
              "runtime production health composition should evaluate the registered probes into a ready aggregate snapshot");
}

}  // namespace

int main() {
  try {
    runtime_production_health_composition_registers_ready_probes();
  } catch (const std::exception& ex) {
    std::cerr << "[RuntimeProductionHealthCompositionTest] FAILED: " << ex.what()
              << '\n';
    return 1;
  }

  return 0;
}