#include <exception>
#include <iostream>
#include <string>

#include "dasall/tests/mocks/MockExecutionService.h"
#include "dasall/tests/mocks/MockMemoryStore.h"
#include "dasall/tests/support/TestAssertions.h"

int main() {
  using dasall::tests::mocks::MockExecutionService;
  using dasall::tests::mocks::MockMemoryStore;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  try {
    MockExecutionService execution_service;
    MockMemoryStore contract_snapshot;

    execution_service.set_handler([&contract_snapshot](const std::string& action) {
      contract_snapshot.write("last_action", action);
      return action == "validate-contract";
    });

    const bool result = execution_service.execute("validate-contract");

    assert_true(result, "execution service should return success for contract smoke test");
    assert_equal(1, execution_service.call_count(), "execution service should be called once");
    assert_equal("validate-contract",
                 contract_snapshot.read("last_action"),
                 "contract snapshot should record last action");
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
