#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <type_traits>

#include "watchdog/IHeartbeatSource.h"
#include "support/TestAssertions.h"

namespace {

class NullHeartbeatSource final : public dasall::infra::watchdog::IHeartbeatSource {
 public:
  dasall::infra::watchdog::HeartbeatSourceEmissionResult emit_heartbeat(
      std::string_view entity_id) override {
    if (entity_id.empty()) {
      return dasall::infra::watchdog::HeartbeatSourceEmissionResult::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          "heartbeat source requires a concrete entity id placeholder",
          "watchdog.emit_heartbeat",
          "NullHeartbeatSource");
    }

    last_entity_id_ = std::string(entity_id);
    return dasall::infra::watchdog::HeartbeatSourceEmissionResult::success();
  }

  [[nodiscard]] dasall::infra::watchdog::HeartbeatSourceDescribeResult describe() const override {
    return dasall::infra::watchdog::HeartbeatSourceDescribeResult::failure(
        dasall::contracts::ResultCode::ValidationFieldMissing,
        "heartbeat source describe remains deferred until WatchedEntityDescriptor is frozen",
        "watchdog.describe",
        "NullHeartbeatSource");
  }

  [[nodiscard]] std::string_view last_entity_id() const {
    return last_entity_id_;
  }

 private:
  std::string last_entity_id_;
};

void test_heartbeat_source_interface_freezes_emit_and_private_describe_boundary() {
  using dasall::infra::watchdog::HeartbeatSourceDescribeResult;
  using dasall::infra::watchdog::HeartbeatSourceEmissionResult;
  using dasall::infra::watchdog::IHeartbeatSource;
  using dasall::infra::watchdog::WatchedEntityDescriptor;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(&IHeartbeatSource::emit_heartbeat),
                               HeartbeatSourceEmissionResult (IHeartbeatSource::*)(
                                   std::string_view)>);
  static_assert(std::is_same_v<decltype(&IHeartbeatSource::describe),
                               HeartbeatSourceDescribeResult (IHeartbeatSource::*)()
                                   const>);
  static_assert(std::is_same_v<decltype(HeartbeatSourceDescribeResult{}.descriptor),
                               std::shared_ptr<const WatchedEntityDescriptor>>);

  NullHeartbeatSource source;

  const auto emit_result = source.emit_heartbeat("runtime.main_loop");
  assert_true(emit_result.ok,
              "IHeartbeatSource should accept emit_heartbeat(entity_id) as the frozen minimal source API");
  assert_true(source.last_entity_id() == "runtime.main_loop",
              "heartbeat emission should preserve the caller-supplied entity id placeholder");
}

void test_heartbeat_source_interface_rejects_empty_entity_ids_and_maps_failures() {
  using dasall::tests::support::assert_true;

  NullHeartbeatSource source;

  const auto emit_result = source.emit_heartbeat("");
  assert_true(!emit_result.ok,
              "IHeartbeatSource should reject empty entity ids before ingestor/object details are frozen");
  assert_true(emit_result.references_only_contract_error_types(),
              "heartbeat source emission failures should remain expressible through contracts ResultCode/ErrorInfo");

  const auto describe_result = source.describe();
  assert_true(!describe_result.ok,
              "IHeartbeatSource should keep describe() explicit even before WatchedEntityDescriptor is frozen");
  assert_true(describe_result.references_only_contract_error_types(),
              "heartbeat source describe failures should remain expressible through contracts ResultCode/ErrorInfo");
}

}  // namespace

int main() {
  try {
    test_heartbeat_source_interface_freezes_emit_and_private_describe_boundary();
    test_heartbeat_source_interface_rejects_empty_entity_ids_and_maps_failures();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}