#include <exception>
#include <iostream>
#include <string>

#include "watchdog/HeartbeatRegistry.h"
#include "watchdog/WatchdogErrors.h"
#include "support/TestAssertions.h"

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

void test_heartbeat_registry_rejects_duplicate_entity_ids_and_keeps_first_entry() {
  using dasall::contracts::ResultCode;
  using dasall::infra::watchdog::watchdog_error_code_name;
  using dasall::infra::watchdog::HeartbeatRegistry;
  using dasall::infra::watchdog::WatchdogErrorCode;
  using dasall::tests::support::assert_true;

  HeartbeatRegistry registry(2U);

  const auto registered = registry.register_entity(
      make_descriptor("runtime.main_loop", "runtime"));
  assert_true(registered.ok && registered.total_entities == 1U &&
                  registry.size() == 1U,
              "HeartbeatRegistry should accept the first valid entity registration and expose the updated entity total");

  const auto duplicate = registry.register_entity(
      make_descriptor("runtime.main_loop", "runtime.shadow"));
  assert_true(!duplicate.ok && duplicate.references_only_contract_error_types() &&
                  duplicate.watchdog_code.has_value() &&
                  *duplicate.watchdog_code == WatchdogErrorCode::EntityDuplicate &&
                  duplicate.result_code.has_value() &&
                  *duplicate.result_code == ResultCode::ValidationFieldMissing &&
                  duplicate.error.has_value() &&
                  duplicate.error->details.message.find(
                      std::string(watchdog_error_code_name(WatchdogErrorCode::EntityDuplicate))) != std::string::npos &&
                  registry.size() == 1U,
              "HeartbeatRegistry should reject duplicate entity_id registrations through INF_E_WATCHDOG_ENTITY_DUPLICATE and preserve the first entry");

  const auto query = registry.query_entity("runtime.main_loop");
  assert_true(query.ok && query.found && query.descriptor.owner_module == "runtime",
              "HeartbeatRegistry should return the first registered descriptor when query_entity resolves an existing entity_id");
}

void test_heartbeat_registry_rejects_missing_unregister_and_enforces_max_entities() {
  using dasall::contracts::ResultCode;
  using dasall::infra::watchdog::watchdog_error_code_name;
  using dasall::infra::watchdog::HeartbeatRegistry;
  using dasall::infra::watchdog::WatchdogErrorCode;
  using dasall::tests::support::assert_true;

  HeartbeatRegistry registry(2U);
  assert_true(registry.register_entity(make_descriptor("runtime.main_loop", "runtime")).ok,
              "HeartbeatRegistry should accept the first entity before max_entities is exercised");
  assert_true(registry.register_entity(make_descriptor("daemon.poller", "daemon")).ok,
              "HeartbeatRegistry should accept the second entity before max_entities is reached");

  const auto overflow = registry.register_entity(
      make_descriptor("gateway.uplink", "gateway"));
  assert_true(!overflow.ok && overflow.references_only_contract_error_types() &&
                  !overflow.watchdog_code.has_value() &&
                  overflow.result_code.has_value() &&
                  *overflow.result_code == ResultCode::ValidationFieldMissing &&
                  overflow.error.has_value() &&
                  overflow.error->details.message.find("max_entities") != std::string::npos &&
                  registry.size() == 2U,
              "HeartbeatRegistry should stop accepting new entities once max_entities has been reached");

  const auto missing = registry.unregister_entity("missing.entity");
  assert_true(!missing.ok && missing.references_only_contract_error_types() &&
                  missing.watchdog_code.has_value() &&
                  *missing.watchdog_code == WatchdogErrorCode::EntityNotFound &&
                  missing.result_code.has_value() &&
                  *missing.result_code == ResultCode::ValidationFieldMissing &&
                  missing.error.has_value() &&
                  missing.error->details.message.find(
                      std::string(watchdog_error_code_name(WatchdogErrorCode::EntityNotFound))) != std::string::npos,
              "HeartbeatRegistry should reject unregister_entity for unknown entity_id through INF_E_WATCHDOG_ENTITY_NOT_FOUND");
}

}  // namespace

int main() {
  try {
    test_heartbeat_registry_rejects_duplicate_entity_ids_and_keeps_first_entry();
    test_heartbeat_registry_rejects_missing_unregister_and_enforces_max_entities();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}