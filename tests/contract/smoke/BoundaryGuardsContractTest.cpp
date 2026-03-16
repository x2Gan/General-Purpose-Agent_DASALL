#include <exception>
#include <iostream>
#include <string>

#include "boundary/BoundaryGuards.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

void test_stable_object_is_allowed_into_stable_boundary() {
  using dasall::contracts::BoundaryGuardDecision;
  using dasall::contracts::ContractObject;
  using dasall::contracts::evaluate_stable_boundary;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const auto result = evaluate_stable_boundary(ContractObject::AgentRequest);

  assert_true(result.allowed,
              "stable object should be admitted by boundary guard");
  assert_equal(static_cast<int>(BoundaryGuardDecision::AllowStable),
               static_cast<int>(result.decision),
               "stable object should return allow decision");
}

void test_blocked_object_is_rejected_from_stable_boundary() {
  using dasall::contracts::BoundaryGuardDecision;
  using dasall::contracts::ContractObject;
  using dasall::contracts::evaluate_stable_boundary;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const auto result = evaluate_stable_boundary(ContractObject::MemoryEvidence);

  // Negative case: blocked objects must always be rejected.
  assert_true(!result.allowed,
              "blocked object must not be admitted by boundary guard");
  assert_equal(static_cast<int>(BoundaryGuardDecision::RejectBlocked),
               static_cast<int>(result.decision),
               "blocked object should return blocked decision");
  assert_equal("blocked objects cannot enter stable contracts",
               std::string(result.reason),
               "blocked object should return blocked reason");
}

void test_deferred_object_is_rejected_until_subdomain_freeze() {
  using dasall::contracts::BoundaryGuardDecision;
  using dasall::contracts::ContractObject;
  using dasall::contracts::evaluate_stable_boundary;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const auto result = evaluate_stable_boundary(ContractObject::ToolRequest);

  // Negative case: deferred objects cannot be promoted in WP01.
  assert_true(!result.allowed,
              "deferred object must not be admitted before subdomain freeze");
  assert_equal(static_cast<int>(BoundaryGuardDecision::RejectDeferred),
               static_cast<int>(result.decision),
               "deferred object should return deferred decision");
  assert_equal("deferred objects cannot enter stable contracts in WP01",
               std::string(result.reason),
               "deferred object should return deferred reason");
}

}  // namespace

int main() {
  try {
    test_stable_object_is_allowed_into_stable_boundary();
    test_blocked_object_is_rejected_from_stable_boundary();
    test_deferred_object_is_rejected_until_subdomain_freeze();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
