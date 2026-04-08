#include <exception>
#include <iostream>
#include <string>

#include "boundary/ObjectBoundaryCatalog.h"
#include "support/TestAssertions.h"

namespace {

void test_catalog_category_counts_match_wp01_freeze() {
  using dasall::contracts::BoundaryCategory;
  using dasall::contracts::count_by_category;
  using dasall::tests::support::assert_equal;

  assert_equal(14,
               static_cast<int>(count_by_category(BoundaryCategory::Stable)),
               "WP01 stable object count must remain 14");
  assert_equal(13,
               static_cast<int>(count_by_category(BoundaryCategory::Blocked)),
               "WP01 blocked object count must remain 13");
  assert_equal(2,
               static_cast<int>(count_by_category(BoundaryCategory::Deferred)),
               "WP01 deferred object count must remain 2");
}

void test_stable_object_identity_and_name_are_available() {
  using dasall::contracts::ContractObject;
  using dasall::contracts::is_stable_object;
  using dasall::contracts::object_name;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  assert_true(is_stable_object(ContractObject::AgentRequest),
              "AgentRequest should be in the stable boundary set");
  assert_equal("AgentRequest",
               std::string(object_name(ContractObject::AgentRequest)),
               "stable object should expose canonical name for traceability");
}

void test_blocked_and_deferred_objects_are_not_misclassified() {
  using dasall::contracts::ContractObject;
  using dasall::contracts::is_blocked_object;
  using dasall::contracts::is_deferred_object;
  using dasall::contracts::is_stable_object;
  using dasall::tests::support::assert_true;

  // Negative case: blocked object must never be reported as stable.
  assert_true(!is_stable_object(ContractObject::MemoryEvidence),
              "MemoryEvidence is blocked and must not appear as stable");

  // Negative case: deferred object must not be downgraded to blocked.
  assert_true(!is_blocked_object(ContractObject::ToolRequest),
              "ToolRequest is deferred and must not be treated as blocked");

  // Positive check paired with the negative check above.
  assert_true(is_deferred_object(ContractObject::ToolRequest),
              "ToolRequest should remain deferred in WP01");
}

}  // namespace

int main() {
  try {
    test_catalog_category_counts_match_wp01_freeze();
    test_stable_object_identity_and_name_are_available();
    test_blocked_and_deferred_objects_are_not_misclassified();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
