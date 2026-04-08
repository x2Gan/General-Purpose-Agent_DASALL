#include <exception>
#include <iostream>
#include <string>

#include "watchdog/HeartbeatIngestor.h"
#include "watchdog/HeartbeatRegistry.h"
#include "watchdog/WatchdogErrors.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

[[nodiscard]] dasall::infra::watchdog::WatchedEntityDescriptor make_descriptor(
    std::string entity_id,
    std::string owner_module) {
  return dasall::infra::watchdog::WatchedEntityDescriptor{
      .entity_id = std::move(entity_id),
      .entity_type = std::string("thread"),
      .owner_module = std::move(owner_module),
      .criticality = dasall::infra::watchdog::WatchdogEntityCriticality::Critical,
      .timeout_ms = 15000,
      .grace_ms = 2000,
  };
}

[[nodiscard]] dasall::infra::watchdog::HeartbeatSample make_sample(
    std::uint64_t seq_no,
    std::int64_t heartbeat_ts,
    std::int64_t deadline_ts) {
  return dasall::infra::watchdog::HeartbeatSample{
      .entity_id = std::string("runtime.main_loop"),
      .heartbeat_ts = heartbeat_ts,
      .deadline_ts = deadline_ts,
      .seq_no = seq_no,
  };
}

void test_heartbeat_ingestor_accepts_registered_entities_and_tracks_latest_sample() {
  using dasall::infra::watchdog::HeartbeatIngestor;
  using dasall::infra::watchdog::HeartbeatRegistry;
  using dasall::tests::support::assert_true;

  HeartbeatRegistry registry;
  assert_true(registry.register_entity(make_descriptor("runtime.main_loop", "runtime")).ok,
              "HeartbeatIngestor tests require the entity to be registered before ingest starts");

  HeartbeatIngestor ingestor(&registry, 2U);
  const auto first = ingestor.ingest(make_sample(8U, 1711958405000, 1711958420000));
  const auto latest = ingestor.latest_sample("runtime.main_loop");
  const auto status = ingestor.status();

  assert_true(first.ok && first.accepted && latest.ok && latest.has_sample &&
                  latest.sample.seq_no == 8U && latest.sample.deadline_ts == 1711958420000 &&
                  status.is_valid() && status.accepted_total == 1U &&
                  status.stale_drop_total == 0U && ingestor.tracked_entity_count() == 1U,
              "HeartbeatIngestor should accept the first heartbeat for a registered entity and expose it as the latest sample");
}

void test_heartbeat_ingestor_rejects_stale_samples_and_records_observable_drop_counts() {
  using dasall::contracts::ResultCode;
  using dasall::infra::watchdog::watchdog_error_code_name;
  using dasall::infra::watchdog::HeartbeatIngestor;
  using dasall::infra::watchdog::HeartbeatRegistry;
  using dasall::infra::watchdog::WatchdogErrorCode;
  using dasall::tests::support::assert_true;

  HeartbeatRegistry registry;
  assert_true(registry.register_entity(make_descriptor("runtime.main_loop", "runtime")).ok,
              "HeartbeatIngestor stale sample tests require a registered entity");

  HeartbeatIngestor ingestor(&registry, 2U);
  assert_true(ingestor.ingest(make_sample(8U, 1711958405000, 1711958420000)).ok,
              "HeartbeatIngestor should accept the first monotonic sample before stale-drop behavior is exercised");

  const auto stale = ingestor.ingest(make_sample(7U, 1711958404000, 1711958419000));
  const auto status = ingestor.status();
  const auto latest = ingestor.latest_sample("runtime.main_loop");

  assert_true(!stale.ok && stale.references_only_contract_error_types() &&
                  stale.watchdog_code.has_value() &&
                  *stale.watchdog_code == WatchdogErrorCode::HeartbeatStale &&
                  stale.result_code.has_value() &&
                  *stale.result_code == ResultCode::ValidationFieldMissing &&
                  stale.error.has_value() &&
                  stale.error->details.message.find(
                      std::string(watchdog_error_code_name(WatchdogErrorCode::HeartbeatStale))) != std::string::npos &&
                  status.is_valid() && status.stale_drop_total == 1U &&
                  status.rejected_total == 1U && latest.ok && latest.sample.seq_no == 8U,
              "HeartbeatIngestor should reject out-of-order samples through INF_E_WATCHDOG_HEARTBEAT_STALE and keep the last accepted sample intact");
}

void test_heartbeat_ingestor_rejects_unregistered_entities_and_keeps_failures_observable() {
  using dasall::contracts::ResultCode;
  using dasall::infra::watchdog::watchdog_error_code_name;
  using dasall::infra::watchdog::HeartbeatIngestor;
  using dasall::infra::watchdog::HeartbeatRegistry;
  using dasall::infra::watchdog::WatchdogErrorCode;
  using dasall::tests::support::assert_true;

  HeartbeatRegistry registry;
  HeartbeatIngestor ingestor(&registry, 2U);

  const auto unknown = ingestor.ingest(make_sample(8U, 1711958405000, 1711958420000));
  const auto status = ingestor.status();

  assert_true(!unknown.ok && unknown.references_only_contract_error_types() &&
                  unknown.watchdog_code.has_value() &&
                  *unknown.watchdog_code == WatchdogErrorCode::EntityNotFound &&
                  unknown.result_code.has_value() &&
                  *unknown.result_code == ResultCode::ValidationFieldMissing &&
                  unknown.error.has_value() &&
                  unknown.error->details.message.find(
                      std::string(watchdog_error_code_name(WatchdogErrorCode::EntityNotFound))) != std::string::npos &&
                  status.is_valid() && status.rejected_total == 1U &&
                  status.last_watchdog_code.has_value() &&
                  *status.last_watchdog_code == WatchdogErrorCode::EntityNotFound,
              "HeartbeatIngestor should reject heartbeats for unknown entity_id values and keep that failure observable in status counters");
}

}  // namespace

int main() {
  try {
    test_heartbeat_ingestor_accepts_registered_entities_and_tracks_latest_sample();
    test_heartbeat_ingestor_rejects_stale_samples_and_records_observable_drop_counts();
    test_heartbeat_ingestor_rejects_unregistered_entities_and_keeps_failures_observable();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}