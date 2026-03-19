// ============================================================================
// WorkerLeaseFieldContractTest.cpp
//
// WP04-T021-B: Field-level contract test for WorkerLeaseGuards.h.
//
// Validates the T021 field-table rules layered on top of the T020 object guard:
//   - required string slots must contain non-whitespace content.
//   - release_reason, if present, must contain non-whitespace content.
//   - renewal_deadline_at must continue respecting the frozen deadline boundary.
// ============================================================================

#include <exception>
#include <iostream>
#include <optional>
#include <string>

#include "dasall/tests/support/TestAssertions.h"
#include "task/WorkerLease.h"
#include "task/WorkerLeaseGuards.h"

namespace {

using dasall::contracts::WorkerLease;
using dasall::contracts::validate_worker_lease_field_rules;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

WorkerLease make_valid_worker_lease() {
  WorkerLease lease;
  lease.lease_id = "lease-021";
  lease.worker_ref = "worker-beta";
  lease.deadline_at = 1710001060000;
  lease.renewal_deadline_at = 1710001055000;
  lease.release_reason = std::nullopt;
  return lease;
}

void test_valid_worker_lease_passes_field_rules() {
  const auto lease = make_valid_worker_lease();
  const auto result = validate_worker_lease_field_rules(lease);

  assert_true(result.ok, "valid WorkerLease should pass field rules");
}

void test_whitespace_only_lease_id_is_rejected() {
  auto lease = make_valid_worker_lease();
  lease.lease_id = "   \t";

  const auto result = validate_worker_lease_field_rules(lease);
  assert_true(!result.ok, "whitespace-only lease_id must be rejected");
  assert_equal("lease_id must contain at least one non-whitespace character",
               std::string(result.reason),
               "lease_id whitespace failure must return canonical reason");
}

void test_whitespace_only_worker_ref_is_rejected() {
  auto lease = make_valid_worker_lease();
  lease.worker_ref = "\n  ";

  const auto result = validate_worker_lease_field_rules(lease);
  assert_true(!result.ok, "whitespace-only worker_ref must be rejected");
  assert_equal(
      "worker_ref must contain at least one non-whitespace character",
      std::string(result.reason),
      "worker_ref whitespace failure must return canonical reason");
}

void test_whitespace_only_release_reason_is_rejected() {
  auto lease = make_valid_worker_lease();
  lease.release_reason = "  \n";

  const auto result = validate_worker_lease_field_rules(lease);
  assert_true(!result.ok,
              "whitespace-only release_reason must be rejected");
  assert_equal(
      "release_reason must contain at least one non-whitespace character when present",
      std::string(result.reason),
      "release_reason whitespace failure must return canonical reason");
}

void test_renewal_deadline_later_than_deadline_is_rejected() {
  auto lease = make_valid_worker_lease();
  lease.renewal_deadline_at = 1710001061000;

  const auto result = validate_worker_lease_field_rules(lease);
  assert_true(!result.ok,
              "renewal_deadline_at later than deadline_at must be rejected");
  assert_equal("renewal_deadline_at must not be later than deadline_at",
               std::string(result.reason),
               "renewal deadline overflow must keep T020 canonical reason");
}

}  // namespace

int main() {
  try {
    test_valid_worker_lease_passes_field_rules();
    test_whitespace_only_lease_id_is_rejected();
    test_whitespace_only_worker_ref_is_rejected();
    test_whitespace_only_release_reason_is_rejected();
    test_renewal_deadline_later_than_deadline_is_rejected();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}