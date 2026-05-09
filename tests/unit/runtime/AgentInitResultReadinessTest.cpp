#include <exception>
#include <iostream>
#include <string>

#include "AgentTypes.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

void test_rejected_init_result_is_not_ready() {
  dasall::runtime::AgentInitResult result;

  assert_equal(static_cast<int>(dasall::runtime::AgentInitReadinessLevel::Rejected),
               static_cast<int>(result.readiness_level()),
               "unaccepted init result should project rejected readiness");
  assert_true(!result.stub_ready(),
              "rejected init result should not project stub-ready");
  assert_true(!result.degraded_ready(),
              "rejected init result should not project degraded-ready");
  assert_true(!result.default_ready(),
              "rejected init result should not project default-ready");
}

void test_stub_runtime_path_projects_stub_ready() {
  dasall::runtime::AgentInitResult result;
  result.accepted = true;
  result.diagnostics = "readiness=stub_runtime_path";

  assert_equal(static_cast<int>(dasall::runtime::AgentInitReadinessLevel::StubReady),
               static_cast<int>(result.readiness_level()),
               "stub runtime path should project stub-ready");
  assert_true(result.stub_ready(),
              "stub runtime path should mark stub-ready");
  assert_equal(std::string("stub-ready"),
               result.readiness_label(),
               "stub runtime path should emit stub-ready label");
}

void test_degraded_init_result_projects_degraded_ready() {
  dasall::runtime::AgentInitResult result;
  result.accepted = true;
  result.degraded = true;
  result.diagnostics = "entrypoint_ready=degraded-ready";

  assert_equal(static_cast<int>(dasall::runtime::AgentInitReadinessLevel::DegradedReady),
               static_cast<int>(result.readiness_level()),
               "accepted degraded init result should project degraded-ready");
  assert_true(result.degraded_ready(),
              "accepted degraded init result should mark degraded-ready");
  assert_true(!result.default_ready(),
              "accepted degraded init result should not mark default-ready");
}

void test_default_ready_is_distinct_from_accepted_only() {
  dasall::runtime::AgentInitResult result;
  result.accepted = true;
  result.diagnostics = "entrypoint_ready=default-ready";

  assert_true(result.is_ready(),
              "compatibility is_ready() should remain accepted-based");
  assert_equal(static_cast<int>(dasall::runtime::AgentInitReadinessLevel::DefaultReady),
               static_cast<int>(result.readiness_level()),
               "accepted non-degraded init result should project default-ready");
  assert_true(result.default_ready(),
              "accepted non-degraded init result should mark default-ready");
}

}  // namespace

int main() {
  try {
    test_rejected_init_result_is_not_ready();
    test_stub_runtime_path_projects_stub_ready();
    test_degraded_init_result_projects_degraded_ready();
    test_default_ready_is_distinct_from_accepted_only();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}