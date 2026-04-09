#include <exception>
#include <iostream>

#include "system/SystemSnapshotLane.h"
#include "support/TestAssertions.h"

namespace {

using dasall::services::ServiceCallContext;
using dasall::services::internal::InternalSnapshotQuery;
using dasall::services::internal::SystemSnapshotLane;
using dasall::services::internal::SystemSnapshotLaneDependencies;

[[nodiscard]] ServiceCallContext make_context() {
  dasall::contracts::RuntimeBudget budget;
  budget.max_latency_ms = 3000;

  return ServiceCallContext{
      .request_id = "req-022",
      .session_id = "session-022",
      .trace_id = "trace-022",
      .tool_call_id = "tool-022",
      .goal_id = "goal-022",
      .budget_guard = budget,
      .deadline_ms = 9000,
  };
}

void test_system_snapshot_lane_returns_internal_snapshot_for_health_usage() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  SystemSnapshotLane lane(SystemSnapshotLaneDependencies{
      .load_infra_health_snapshot = []() { return std::string("{\"status\":\"ok\"}"); },
      .load_platform_snapshot = []() { return std::string("{\"capabilities\":[\"gpio\"]}"); },
      .load_resource_summary = []() { return std::string("{\"cpu\":24,\"mem\":48}"); },
      .load_service_instances = []() { return std::vector<std::string>{"svc.alpha", "svc.beta"}; },
      .now_ms = []() { return 12345U; },
  });

  const auto snapshot = lane.query_snapshot(make_context(), InternalSnapshotQuery{});

  assert_true(snapshot.error_message.empty(), "successful snapshot should not carry error message");
  assert_true(snapshot.infra_health_ok, "successful snapshot should mark infra_health_ok=true");
  assert_true(!snapshot.degraded, "all internal sources present should not degrade snapshot");
  assert_equal(2,
               static_cast<int>(snapshot.service_instances.size()),
               "service registry should be included when requested");
  assert_true(snapshot.snapshot_json.find("\"svc.alpha\"") != std::string::npos,
              "snapshot JSON should include service registry entries");
}

void test_system_snapshot_lane_fails_closed_when_strict_health_has_no_snapshot() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  SystemSnapshotLane lane(SystemSnapshotLaneDependencies{
      .load_infra_health_snapshot = []() { return std::string{}; },
      .load_platform_snapshot = []() { return std::string("{\"capabilities\":[]}"); },
      .load_resource_summary = []() { return std::string("{\"cpu\":24}"); },
      .load_service_instances = []() { return std::vector<std::string>{"svc.alpha"}; },
      .now_ms = []() { return 22334U; },
  });

  const auto snapshot = lane.query_snapshot(make_context(),
                                            InternalSnapshotQuery{
                                                .include_service_registry = true,
                                                .include_platform_snapshot = true,
                                                .strict_health = true,
                                            });

  assert_true(snapshot.snapshot_json.empty(), "strict health failure should not return snapshot JSON");
  assert_true(snapshot.degraded, "strict health failure should mark degraded=true");
  assert_equal(std::string("infra health snapshot unavailable"),
               snapshot.error_message,
               "strict health failure should expose a deterministic internal error");
}

void test_system_snapshot_lane_returns_degraded_snapshot_when_optional_sources_missing() {
  using dasall::tests::support::assert_true;

  SystemSnapshotLane lane(SystemSnapshotLaneDependencies{
      .load_infra_health_snapshot = []() { return std::string("{\"status\":\"ok\"}"); },
      .load_platform_snapshot = []() { return std::string{}; },
      .load_resource_summary = []() { return std::string("{\"cpu\":80}"); },
      .load_service_instances = {},
      .now_ms = []() { return 32334U; },
  });

  const auto snapshot = lane.query_snapshot(make_context(),
                                            InternalSnapshotQuery{
                                                .include_service_registry = true,
                                                .include_platform_snapshot = true,
                                                .strict_health = false,
                                            });

  assert_true(snapshot.error_message.empty(), "degraded snapshot should still be returned");
  assert_true(snapshot.degraded, "missing optional sources should mark snapshot degraded");
  assert_true(snapshot.snapshot_json.find("\"platform_snapshot\":null") != std::string::npos,
              "degraded snapshot should render missing platform data as null");
}

void test_system_snapshot_lane_can_omit_service_registry_from_internal_query() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  int service_registry_loads = 0;
  SystemSnapshotLane lane(SystemSnapshotLaneDependencies{
      .load_infra_health_snapshot = []() { return std::string("{\"status\":\"ok\"}"); },
      .load_platform_snapshot = []() { return std::string("{\"capabilities\":[]}"); },
      .load_resource_summary = []() { return std::string("{\"cpu\":12}"); },
      .load_service_instances = [&]() {
        ++service_registry_loads;
        return std::vector<std::string>{"svc.gamma"};
      },
      .now_ms = []() { return 42334U; },
  });

  const auto snapshot = lane.query_snapshot(make_context(),
                                            InternalSnapshotQuery{
                                                .include_service_registry = false,
                                                .include_platform_snapshot = true,
                                                .strict_health = false,
                                            });

  assert_equal(0,
               service_registry_loads,
               "internal query should skip service registry loader when not requested");
  assert_true(snapshot.service_instances.empty(),
              "service registry omitted query should keep service_instances empty");
}

}  // namespace

int main() {
  try {
    test_system_snapshot_lane_returns_internal_snapshot_for_health_usage();
    test_system_snapshot_lane_fails_closed_when_strict_health_has_no_snapshot();
    test_system_snapshot_lane_returns_degraded_snapshot_when_optional_sources_missing();
    test_system_snapshot_lane_can_omit_service_registry_from_internal_query();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}