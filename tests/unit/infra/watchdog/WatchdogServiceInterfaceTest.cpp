#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <type_traits>

#include "watchdog/IWatchdogService.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

class NullWatchdogService final : public dasall::infra::watchdog::IWatchdogService {
 public:
  dasall::infra::watchdog::WatchdogOperationResult init(
      const dasall::infra::watchdog::WatchdogServiceConfig& config) override {
    if (!config.is_valid()) {
      return dasall::infra::watchdog::WatchdogOperationResult::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          "watchdog init requires a valid typed config seed",
          "watchdog.init",
          "NullWatchdogService");
    }

    initialized_ = true;
    return dasall::infra::watchdog::WatchdogOperationResult::success();
  }

  dasall::infra::watchdog::WatchdogOperationResult start() override {
    if (!initialized_) {
      return dasall::infra::watchdog::WatchdogOperationResult::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          "watchdog start requires init to run first",
          "watchdog.start",
          "NullWatchdogService");
    }

    started_ = true;
    return dasall::infra::watchdog::WatchdogOperationResult::success();
  }

  dasall::infra::watchdog::WatchdogOperationResult stop(std::uint32_t timeout_ms) override {
    if (!started_ || timeout_ms == 0) {
      return dasall::infra::watchdog::WatchdogOperationResult::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          "watchdog stop requires a started service and a non-zero timeout",
          "watchdog.stop",
          "NullWatchdogService");
    }

    started_ = false;
    return dasall::infra::watchdog::WatchdogOperationResult::success();
  }

  dasall::infra::watchdog::WatchdogOperationResult register_entity(
      const dasall::infra::watchdog::WatchedEntityDescriptor&) override {
    return dasall::infra::watchdog::WatchdogOperationResult::success();
  }

  dasall::infra::watchdog::WatchdogOperationResult unregister_entity(
      std::string_view entity_id) override {
    if (entity_id.empty()) {
      return dasall::infra::watchdog::WatchdogOperationResult::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          "watchdog unregister_entity requires a concrete entity id",
          "watchdog.unregister_entity",
          "NullWatchdogService");
    }

    return dasall::infra::watchdog::WatchdogOperationResult::success();
  }

  dasall::infra::watchdog::WatchdogOperationResult heartbeat(
      const dasall::infra::watchdog::HeartbeatSample&) override {
    if (!started_) {
      return dasall::infra::watchdog::WatchdogOperationResult::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          "watchdog heartbeat requires the service to be started first",
          "watchdog.heartbeat",
          "NullWatchdogService");
    }

    return dasall::infra::watchdog::WatchdogOperationResult::success();
  }

  [[nodiscard]] dasall::infra::watchdog::WatchdogSnapshotQueryResult snapshot() const override {
    return dasall::infra::watchdog::WatchdogSnapshotQueryResult::failure(
        dasall::contracts::ResultCode::ValidationFieldMissing,
        "watchdog snapshot remains unavailable until WatchdogSnapshot is frozen",
        "watchdog.snapshot",
        "NullWatchdogService");
  }

 private:
  bool initialized_ = false;
  bool started_ = false;
};

void test_watchdog_service_interface_freezes_lifecycle_and_error_mapping() {
  using dasall::infra::watchdog::IWatchdogService;
  using dasall::infra::watchdog::WatchdogOperationResult;
  using dasall::infra::watchdog::WatchdogServiceConfig;
  using dasall::infra::watchdog::WatchdogSnapshotQueryResult;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(&IWatchdogService::init),
                               WatchdogOperationResult (IWatchdogService::*)(
                                   const WatchdogServiceConfig&)>);
  static_assert(std::is_same_v<decltype(&IWatchdogService::start),
                               WatchdogOperationResult (IWatchdogService::*)()>);
  static_assert(std::is_same_v<decltype(&IWatchdogService::stop),
                               WatchdogOperationResult (IWatchdogService::*)(
                                   std::uint32_t)>);
  static_assert(std::is_same_v<decltype(&IWatchdogService::snapshot),
                               WatchdogSnapshotQueryResult (IWatchdogService::*)()
                                   const>);

  NullWatchdogService service;
  WatchdogServiceConfig config;

  const auto init_result = service.init(config);
  assert_true(init_result.ok,
              "IWatchdogService should accept the frozen watchdog config seed during init");

  const auto start_result = service.start();
  assert_true(start_result.ok,
              "IWatchdogService should expose an explicit start() lifecycle entrypoint");

  const auto stop_result = service.stop(1000);
  assert_true(stop_result.ok,
              "IWatchdogService should expose an explicit stop(timeout_ms) lifecycle entrypoint");
}

void test_watchdog_service_interface_rejects_invalid_lifecycle_inputs_observably() {
  using dasall::infra::watchdog::WatchdogServiceConfig;
  using dasall::tests::support::assert_true;

  NullWatchdogService service;

  WatchdogServiceConfig invalid_config;
  invalid_config.timeout_level_policy =
      dasall::infra::watchdog::WatchdogTimeoutLevelPolicy::Unspecified;

  const auto init_result = service.init(invalid_config);
  assert_true(!init_result.ok,
              "IWatchdogService should reject invalid config seeds before implementation details exist");
  assert_true(init_result.references_only_contract_error_types(),
              "watchdog init failures should remain expressible through contracts ResultCode/ErrorInfo");

  const auto start_result = service.start();
  assert_true(!start_result.ok,
              "IWatchdogService should reject start() before init()");
  assert_true(start_result.references_only_contract_error_types(),
              "watchdog lifecycle failures should remain expressible through contracts ResultCode/ErrorInfo");

  const auto snapshot_result = service.snapshot();
  assert_true(!snapshot_result.ok,
              "IWatchdogService should surface snapshot() as an explicit boundary even before WatchdogSnapshot is frozen");
  assert_true(snapshot_result.references_only_contract_error_types(),
              "watchdog snapshot failures should remain expressible through contracts ResultCode/ErrorInfo");
}

}  // namespace

int main() {
  try {
    test_watchdog_service_interface_freezes_lifecycle_and_error_mapping();
    test_watchdog_service_interface_rejects_invalid_lifecycle_inputs_observably();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}