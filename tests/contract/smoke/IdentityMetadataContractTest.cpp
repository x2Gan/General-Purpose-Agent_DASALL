#include <exception>
#include <iostream>
#include <string>

#include "boundary/IdentityMetadata.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

dasall::contracts::IdentityMetadata make_valid_child_identity_metadata() {
  return dasall::contracts::IdentityMetadata{
      .request_id = std::string("req-001"),
      .session_id = std::string("sess-001"),
      .trace_id = std::string("trace-001"),
      .task_id = std::string("task-child-001"),
      .lease_id = std::string("lease-001"),
      .parent_task_id = std::string("task-parent-001"),
      .is_child_task = true,
  };
}

void test_valid_child_identity_metadata_passes_guard() {
  using dasall::contracts::validate_identity_metadata;
  using dasall::tests::support::assert_true;

  // Positive case: all five IDs are present and child task propagation carries
  // a valid parent_task_id that differs from task_id.
  const auto metadata = make_valid_child_identity_metadata();
  const auto result = validate_identity_metadata(metadata);

  assert_true(result.ok, "valid child identity metadata should pass validation");
}

void test_child_task_without_parent_is_rejected() {
  using dasall::contracts::validate_identity_metadata;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  // Negative case: child task propagation must include a parent_task_id.
  auto metadata = make_valid_child_identity_metadata();
  metadata.parent_task_id = std::nullopt;

  const auto result = validate_identity_metadata(metadata);

  assert_true(!result.ok, "child task without parent_task_id should be rejected");
  assert_equal("parent_task_id is required for child task",
               std::string(result.reason),
               "guard should enforce child task parent propagation rule");
}

void test_parent_task_self_reference_is_rejected() {
  using dasall::contracts::validate_identity_metadata;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  // Negative case: parent_task_id must reference the upstream task and cannot
  // be identical to the current task_id.
  auto metadata = make_valid_child_identity_metadata();
  metadata.parent_task_id = metadata.task_id;

  const auto result = validate_identity_metadata(metadata);

  assert_true(!result.ok, "parent_task_id equal to task_id should be rejected");
  assert_equal("parent_task_id must not equal task_id",
               std::string(result.reason),
               "guard should reject self-referential parent propagation");
}

}  // namespace

int main() {
  try {
    test_valid_child_identity_metadata_passes_guard();
    test_child_task_without_parent_is_rejected();
    test_parent_task_self_reference_is_rejected();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
