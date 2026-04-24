#include <exception>
#include <iostream>
#include <string>

#include "AccessPolicyGate.h"
#include "support/TestAssertions.h"

namespace {

void denies_task_query_when_policy_backend_is_unavailable() {
  using dasall::access::AccessPolicyEvaluationInput;
  using dasall::access::AccessPolicyGate;
  using dasall::access::PolicyBackendSnapshot;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  AccessPolicyEvaluationInput input;
  input.authentication.authenticated = true;
  input.authentication.subject_identity.actor_ref = "user://tenant-a/alice";
  input.authentication.subject_identity.auth_method = "JWT";
  input.packet.packet_id = "pkt-query-001";
  input.packet.entry_type = "gateway";
  input.packet.protocol_kind = "http";

  PolicyBackendSnapshot backend;
  backend.backend_available = false;

  AccessPolicyGate gate;
  const auto result = gate.evaluate_task_query(input, "task-001", backend);
  assert_true(result.denied(), "task query should deny when backend is unavailable");
  assert_true(result.reject_reason.has_value(),
              "backend failure deny should carry explicit reason");
  assert_equal(std::string("policy_backend_unavailable"), *result.reject_reason,
               "backend unavailable should map to policy_backend_unavailable");
}

}  // namespace

int main() {
  try {
    denies_task_query_when_policy_backend_is_unavailable();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
