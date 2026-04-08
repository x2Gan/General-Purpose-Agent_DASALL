#include <exception>
#include <iostream>
#include <string>

#include "watchdog/WatchdogServiceFacade.h"
#include "watchdog/WatchdogErrors.h"
#include "watchdog/WatchdogSnapshot.h"
#include "support/TestAssertions.h"

namespace {

void test_watchdog_service_facade_rejects_uninitialized_paths() {
  using dasall::contracts::ResultCode;
  using dasall::infra::watchdog::WatchdogServiceFacade;
  using dasall::tests::support::assert_true;

  WatchdogServiceFacade facade;

  const auto start_result = facade.start();
  assert_true(!start_result.ok && start_result.references_only_contract_error_types() &&
                  start_result.result_code.has_value() &&
                  *start_result.result_code == ResultCode::RuntimeRetryExhausted,
              "WatchdogServiceFacade should reject start before init and stay inside the contracts runtime failure boundary");

  const auto snapshot_result = facade.snapshot();
  assert_true(!snapshot_result.ok &&
                  snapshot_result.references_only_contract_error_types() &&
                  snapshot_result.result_code.has_value() &&
                  *snapshot_result.result_code == ResultCode::ValidationFieldMissing,
              "WatchdogServiceFacade should reject snapshot before init has produced the first placeholder snapshot");
}

void test_watchdog_service_facade_tracks_lifecycle_and_snapshots() {
  using dasall::infra::watchdog::WatchdogServiceConfig;
  using dasall::infra::watchdog::WatchdogServiceFacade;
  using dasall::tests::support::assert_true;

  WatchdogServiceFacade facade;

  const auto init_result = facade.init(WatchdogServiceConfig{});
  assert_true(init_result.ok && facade.has_snapshot() &&
                  facade.lifecycle_state_name() == "initialized",
              "WatchdogServiceFacade should accept the default valid config and publish an initialized placeholder snapshot");

  const auto initialized_snapshot = facade.snapshot();
  assert_true(initialized_snapshot.ok && initialized_snapshot.has_snapshot() &&
                  initialized_snapshot.snapshot->has_consistent_counts(),
              "WatchdogServiceFacade should expose a consistent placeholder snapshot immediately after init succeeds");

  const auto start_result = facade.start();
  assert_true(start_result.ok && facade.lifecycle_state_name() == "started",
              "WatchdogServiceFacade should transition to started after start succeeds");

  const auto started_snapshot = facade.snapshot();
  assert_true(started_snapshot.ok &&
                  started_snapshot.snapshot->is_newer_than(*initialized_snapshot.snapshot),
              "WatchdogServiceFacade should advance snapshot version metadata when the lifecycle enters started");

  const auto stop_result = facade.stop(250U);
  assert_true(stop_result.ok && facade.lifecycle_state_name() == "stopped" &&
                  facade.last_stop_timeout_ms().has_value() &&
                  *facade.last_stop_timeout_ms() == 250U,
              "WatchdogServiceFacade should preserve the caller-provided graceful stop timeout when shutdown succeeds");

  const auto stopped_snapshot = facade.snapshot();
  assert_true(stopped_snapshot.ok &&
                  stopped_snapshot.snapshot->is_newer_than(*started_snapshot.snapshot),
              "WatchdogServiceFacade should preserve a newer placeholder snapshot after graceful stop completes");
}

void test_watchdog_service_facade_rejects_invalid_config_and_zero_stop_timeout() {
  using dasall::contracts::ResultCode;
  using dasall::infra::watchdog::WatchdogServiceConfig;
  using dasall::infra::watchdog::WatchdogServiceFacade;
  using dasall::tests::support::assert_true;

  WatchdogServiceFacade invalid_facade;
  auto invalid_config = WatchdogServiceConfig{};
  invalid_config.scan_interval_ms = 0;

  const auto invalid_init = invalid_facade.init(invalid_config);
  assert_true(!invalid_init.ok && invalid_init.references_only_contract_error_types() &&
                  invalid_init.result_code.has_value() &&
                  *invalid_init.result_code == ResultCode::ValidationFieldMissing,
              "WatchdogServiceFacade should reject invalid config values before the lifecycle leaves created");

  WatchdogServiceFacade stop_facade;
  assert_true(stop_facade.init(WatchdogServiceConfig{}).ok && stop_facade.start().ok,
              "WatchdogServiceFacade should reach started before zero-timeout shutdown is exercised");

  const auto invalid_stop = stop_facade.stop(0U);
  assert_true(!invalid_stop.ok && invalid_stop.references_only_contract_error_types() &&
                  invalid_stop.result_code.has_value() &&
                  *invalid_stop.result_code == ResultCode::ValidationFieldMissing,
              "WatchdogServiceFacade should reject zero timeout so graceful shutdown keeps an explicit budget");
}

void test_watchdog_service_facade_routes_entity_registration_through_registry() {
  using dasall::contracts::ResultCode;
  using dasall::infra::watchdog::watchdog_error_code_name;
  using dasall::infra::watchdog::WatchdogErrorCode;
  using dasall::infra::watchdog::WatchedEntityDescriptor;
  using dasall::infra::watchdog::WatchdogEntityCriticality;
  using dasall::infra::watchdog::WatchdogServiceConfig;
  using dasall::infra::watchdog::WatchdogServiceFacade;
  using dasall::tests::support::assert_true;

  WatchdogServiceFacade facade;
  assert_true(facade.init(WatchdogServiceConfig{.max_entities = 2}).ok,
              "WatchdogServiceFacade should initialize before registry-backed registration is exercised");

  const WatchedEntityDescriptor descriptor{
      .entity_id = std::string("runtime.main_loop"),
      .entity_type = std::string("thread"),
      .owner_module = std::string("runtime"),
      .criticality = WatchdogEntityCriticality::Critical,
      .timeout_ms = 15000,
      .grace_ms = 2000,
  };

  const auto first_registration = facade.register_entity(descriptor);
  assert_true(first_registration.ok && facade.snapshot().snapshot->total_entities == 1U,
              "WatchdogServiceFacade should expose the registry-backed entity total after the first registration succeeds");

  const auto duplicate_registration = facade.register_entity(descriptor);
  assert_true(!duplicate_registration.ok &&
                  duplicate_registration.references_only_contract_error_types() &&
                  duplicate_registration.result_code.has_value() &&
                  *duplicate_registration.result_code == ResultCode::ValidationFieldMissing &&
                  duplicate_registration.error.has_value() &&
                  duplicate_registration.error->details.message.find(
                      std::string(watchdog_error_code_name(WatchdogErrorCode::EntityDuplicate))) != std::string::npos &&
                  facade.snapshot().snapshot->total_entities == 1U,
              "WatchdogServiceFacade should surface duplicate registrations through the frozen INF_E_WATCHDOG_ENTITY_DUPLICATE private error name without mutating registry size");

  const auto removed = facade.unregister_entity("runtime.main_loop");
  assert_true(removed.ok && facade.snapshot().snapshot->total_entities == 0U,
              "WatchdogServiceFacade should drop the registry-backed entity total after unregister succeeds");
}

        void test_watchdog_service_facade_routes_heartbeat_ingest_and_rejects_stale_samples() {
          using dasall::contracts::ResultCode;
          using dasall::infra::watchdog::HeartbeatSample;
          using dasall::infra::watchdog::watchdog_error_code_name;
          using dasall::infra::watchdog::WatchedEntityDescriptor;
          using dasall::infra::watchdog::WatchdogEntityCriticality;
          using dasall::infra::watchdog::WatchdogErrorCode;
          using dasall::infra::watchdog::WatchdogServiceConfig;
          using dasall::infra::watchdog::WatchdogServiceFacade;
          using dasall::tests::support::assert_true;

          WatchdogServiceFacade facade;
          assert_true(facade.init(WatchdogServiceConfig{}).ok,
                "WatchdogServiceFacade should initialize before heartbeat ingest is exercised");
          assert_true(facade.register_entity(WatchedEntityDescriptor{
                  .entity_id = std::string("runtime.main_loop"),
                  .entity_type = std::string("thread"),
                  .owner_module = std::string("runtime"),
                  .criticality = WatchdogEntityCriticality::Critical,
                  .timeout_ms = 15000,
                  .grace_ms = 2000,
                }).ok,
                "WatchdogServiceFacade should register the watched entity before heartbeats are ingested");
          assert_true(facade.start().ok,
                "WatchdogServiceFacade should enter started before the first heartbeat is ingested");

          const auto first_heartbeat = facade.heartbeat(HeartbeatSample{
            .entity_id = std::string("runtime.main_loop"),
            .heartbeat_ts = 1711958405000,
            .deadline_ts = 1711958420000,
            .seq_no = 8,
          });
          assert_true(first_heartbeat.ok,
                "WatchdogServiceFacade should accept the first monotonic heartbeat after the entity has been registered and the lifecycle is started");

          const auto stale_heartbeat = facade.heartbeat(HeartbeatSample{
            .entity_id = std::string("runtime.main_loop"),
            .heartbeat_ts = 1711958404000,
            .deadline_ts = 1711958419000,
            .seq_no = 7,
          });
          assert_true(!stale_heartbeat.ok &&
                  stale_heartbeat.references_only_contract_error_types() &&
                  stale_heartbeat.result_code.has_value() &&
                  *stale_heartbeat.result_code == ResultCode::ValidationFieldMissing &&
                  stale_heartbeat.error.has_value() &&
                  stale_heartbeat.error->details.message.find(
                    std::string(watchdog_error_code_name(WatchdogErrorCode::HeartbeatStale))) != std::string::npos,
                "WatchdogServiceFacade should surface stale heartbeat rejection through the frozen INF_E_WATCHDOG_HEARTBEAT_STALE private error name");
        }

}  // namespace

int main() {
  try {
    test_watchdog_service_facade_rejects_uninitialized_paths();
    test_watchdog_service_facade_tracks_lifecycle_and_snapshots();
    test_watchdog_service_facade_rejects_invalid_config_and_zero_stop_timeout();
    test_watchdog_service_facade_routes_entity_registration_through_registry();
    test_watchdog_service_facade_routes_heartbeat_ingest_and_rejects_stale_samples();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}