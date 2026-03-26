#include <exception>
#include <iostream>
#include <string>

#include "IInfrastructureService.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

void test_infra_service_facade_enforces_lifecycle_order() {
  using dasall::infra::InfraCommandRequest;
  using dasall::infra::InfraServiceFacade;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  InfraServiceFacade facade;

  const auto start_before_init = facade.start();
  assert_true(!start_before_init.ok,
              "facade should reject start before init");
  assert_true(start_before_init.references_only_contract_error_types(),
              "lifecycle failures should use contracts error types only");

  const auto init_result = facade.init({.profile = "edge_balanced"});
  assert_true(init_result.ok,
              "facade should initialize from created state");
  assert_equal(std::string("initialized"),
               std::string(facade.lifecycle_state_name()),
               "facade should advance to initialized state after init");

  const auto execute_before_start = facade.execute({.name = "health.evaluate"});
  assert_true(!execute_before_start.ok,
              "facade should reject execute before start");

  const auto start_result = facade.start();
  assert_true(start_result.ok,
              "facade should start after init");
  assert_equal(std::string("started"),
               std::string(facade.lifecycle_state_name()),
               "facade should advance to started state after start");

  const auto execute_result = facade.execute(InfraCommandRequest{.name = "diagnostics.export"});
  assert_true(execute_result.ok,
              "facade should accept named commands after start");

  const auto stop_result = facade.stop(5000);
  assert_true(stop_result.ok,
              "facade should stop from started state");
  assert_equal(std::string("stopped"),
               std::string(facade.lifecycle_state_name()),
               "facade should advance to stopped state after stop");
}

void test_infra_service_facade_rejects_empty_config_or_command_inputs() {
  using dasall::infra::InfraServiceFacade;
  using dasall::tests::support::assert_true;

  InfraServiceFacade empty_config_facade;
  const auto invalid_init = empty_config_facade.init({.profile = ""});
  assert_true(!invalid_init.ok,
              "facade should reject empty profile names in the skeleton config");
  assert_true(invalid_init.references_only_contract_error_types(),
              "config validation failures should stay inside contracts error types");

  InfraServiceFacade empty_command_facade;
  const auto init_result = empty_command_facade.init({.profile = "desktop_full"});
  assert_true(init_result.ok,
              "facade should initialize before testing empty command validation");
  const auto start_result = empty_command_facade.start();
  assert_true(start_result.ok,
              "facade should start before testing empty command validation");

  const auto invalid_execute = empty_command_facade.execute({.name = ""});
  assert_true(!invalid_execute.ok,
              "facade should reject empty command names");
  assert_true(invalid_execute.references_only_contract_error_types(),
              "execute validation failures should stay inside contracts error types");
}

}  // namespace

int main() {
  try {
    test_infra_service_facade_enforces_lifecycle_order();
    test_infra_service_facade_rejects_empty_config_or_command_inputs();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}