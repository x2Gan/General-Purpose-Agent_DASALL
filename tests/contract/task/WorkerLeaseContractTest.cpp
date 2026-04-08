#include <exception>
#include <iostream>
#include <string>
#include <type_traits>
#include <utility>

#include "support/TestAssertions.h"
#include "task/WorkerLease.h"
#include "task/WorkerLeaseGuards.h"

namespace {

using dasall::contracts::WorkerLease;
using dasall::contracts::validate_worker_lease_boundary;
using dasall::contracts::validate_worker_lease_forbidden_field;
using dasall::contracts::validate_worker_lease_required_fields;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

template <typename T, typename = void>
struct has_session_id : std::false_type {};

template <typename T>
struct has_session_id<T, std::void_t<decltype(std::declval<T>().session_id)>>
    : std::true_type {};

template <typename T, typename = void>
struct has_checkpoint_ref : std::false_type {};

template <typename T>
struct has_checkpoint_ref<
    T,
    std::void_t<decltype(std::declval<T>().checkpoint_ref)>> : std::true_type {
};

template <typename T, typename = void>
struct has_agent_result : std::false_type {};

template <typename T>
struct has_agent_result<T, std::void_t<decltype(std::declval<T>().agent_result)>>
    : std::true_type {};

WorkerLease make_valid_worker_lease() {
  WorkerLease lease;
  lease.lease_id = "lease-020";
  lease.worker_ref = "worker-alpha";
  lease.deadline_at = 1710000060000;
  lease.renewal_deadline_at = 1710000055000;
  lease.release_reason = std::nullopt;
  return lease;
}

void test_valid_worker_lease_passes_required_fields_guard() {
  const auto lease = make_valid_worker_lease();
  const auto result = validate_worker_lease_required_fields(lease);
  assert_true(result.ok,
              "valid worker lease should pass required fields guard");
}

void test_valid_worker_lease_passes_boundary_guard() {
  const auto lease = make_valid_worker_lease();
  const auto result = validate_worker_lease_boundary(lease);
  assert_true(result.ok, "valid worker lease should pass boundary guard");
}

void test_missing_lease_id_fails_required_fields_guard() {
  auto lease = make_valid_worker_lease();
  lease.lease_id = std::nullopt;

  const auto result = validate_worker_lease_required_fields(lease);
  assert_true(!result.ok, "missing lease_id must fail required guard");
  assert_equal("lease_id is required and must be non-empty",
               std::string(result.reason),
               "missing lease_id must return canonical reason");
}

void test_non_positive_deadline_fails_required_fields_guard() {
  auto lease = make_valid_worker_lease();
  lease.deadline_at = 0;

  const auto result = validate_worker_lease_required_fields(lease);
  assert_true(!result.ok, "non-positive deadline_at must fail required guard");
  assert_equal("deadline_at is required and must be a positive timestamp",
               std::string(result.reason),
               "invalid deadline_at must return canonical reason");
}

void test_renewal_deadline_later_than_deadline_fails_boundary_guard() {
  auto lease = make_valid_worker_lease();
  lease.renewal_deadline_at = 1710000061000;

  const auto result = validate_worker_lease_boundary(lease);
  assert_true(!result.ok,
              "renewal_deadline_at later than deadline_at must fail boundary guard");
  assert_equal("renewal_deadline_at must not be later than deadline_at",
               std::string(result.reason),
               "renewal boundary failure must return canonical reason");
}

void test_checkpoint_ref_alias_is_rejected_by_worker_lease_boundary_guard() {
  const auto result = validate_worker_lease_forbidden_field("checkpoint_ref");
  assert_true(!result.ok, "checkpoint_ref must be rejected for WorkerLease");
  assert_equal("worker lease must not become a checkpoint or resume entry",
               std::string(result.reason),
               "checkpoint alias rejection must keep normalized reason");
}

void test_compile_time_shape_does_not_reuse_global_or_result_fields() {
  static_assert(!has_session_id<WorkerLease>::value,
                "WorkerLease must not expose session_id");
  static_assert(!has_checkpoint_ref<WorkerLease>::value,
                "WorkerLease must not expose checkpoint_ref");
  static_assert(!has_agent_result<WorkerLease>::value,
                "WorkerLease must not expose agent_result");
}

}  // namespace

int main() {
  try {
    test_valid_worker_lease_passes_required_fields_guard();
    test_valid_worker_lease_passes_boundary_guard();
    test_missing_lease_id_fails_required_fields_guard();
    test_non_positive_deadline_fails_required_fields_guard();
    test_renewal_deadline_later_than_deadline_fails_boundary_guard();
    test_checkpoint_ref_alias_is_rejected_by_worker_lease_boundary_guard();
    test_compile_time_shape_does_not_reuse_global_or_result_fields();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}