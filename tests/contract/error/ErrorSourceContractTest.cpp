#include <exception>
#include <iostream>
#include <string>

#include "support/TestAssertions.h"
#include "error/ErrorSourceGuards.h"

namespace {

dasall::contracts::ErrorSourceRefSet make_valid_error_source_ref_set() {
  using dasall::contracts::ErrorSourceRefSet;

  return ErrorSourceRefSet{
      .refs = {
          {.primary = true, .ref_type = "observation", .ref_id = "obs-1"},
          {.primary = false, .ref_type = "tool_call", .ref_id = "tc-1"},
          {.primary = false, .ref_type = "worker_task", .ref_id = "wt-1"},
          {.primary = false, .ref_type = "checkpoint", .ref_id = "cp-1"},
      },
  };
}

void test_primary_unique_and_four_types_are_accepted() {
  using dasall::contracts::validate_error_source_refs;
  using dasall::tests::support::assert_true;

  // Positive case: one primary plus the four frozen ref_type kinds with
  // non-empty IDs should pass validation.
  const auto source_refs = make_valid_error_source_ref_set();
  const auto result = validate_error_source_refs(source_refs);

  assert_true(result.ok, "valid source refs should pass guard validation");
}

void test_multiple_primary_refs_are_rejected() {
  using dasall::contracts::validate_error_source_refs;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  // Negative case: source refs must have exactly one primary.
  auto source_refs = make_valid_error_source_ref_set();
  source_refs.refs[1].primary = true;

  const auto result = validate_error_source_refs(source_refs);

  assert_true(!result.ok, "multiple primary refs should be rejected");
  assert_equal("exactly one primary source ref is required",
               std::string(result.reason),
               "guard should report primary uniqueness failure");
}

void test_empty_ref_id_is_rejected() {
  using dasall::contracts::validate_error_source_refs;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  // Negative case: each reference must provide a non-empty ref_id.
  auto source_refs = make_valid_error_source_ref_set();
  source_refs.refs[2].ref_id.clear();

  const auto result = validate_error_source_refs(source_refs);

  assert_true(!result.ok, "empty ref_id should be rejected");
  assert_equal("source ref id is required",
               std::string(result.reason),
               "guard should report missing ref_id");
}

}  // namespace

int main() {
  try {
    test_primary_unique_and_four_types_are_accepted();
    test_multiple_primary_refs_are_rejected();
    test_empty_ref_id_is_rejected();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
