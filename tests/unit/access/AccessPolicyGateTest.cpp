#include <exception>
#include <iostream>
#include <string>

#include "AccessPolicyGate.h"
#include "support/TestAssertions.h"

namespace {

[[nodiscard]] dasall::access::AccessPolicyEvaluationInput make_authenticated_submit_input() {
  dasall::access::AccessPolicyEvaluationInput input;
  input.authentication.authenticated = true;
  input.authentication.subject_identity.actor_ref = "user://tenant-a/alice";
  input.authentication.subject_identity.auth_method = "JWT";
  input.packet.packet_id = "pkt-001";
  input.packet.entry_type = "gateway";
  input.packet.protocol_kind = "http";
  input.packet.payload = "{}";
  return input;
}

void allows_submit_when_policy_backend_emits_allow_proof() {
  using dasall::access::AccessPolicyGate;
  using dasall::access::PolicyBackendSnapshot;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  AccessPolicyGate gate;
  PolicyBackendSnapshot backend;
  backend.allow_submit = true;
  backend.decision_ref = "policy://access/allow-submit";

  const auto result = gate.evaluate_submit(make_authenticated_submit_input(), backend);
  assert_true(result.allowed, "submit should be allowed with explicit allow proof");
  assert_equal(std::string("Allow"), result.decision_proof.decision,
               "allowed submit should emit Allow decision");
  assert_equal(std::string("allow_proof"), result.decision_proof.reason_code,
               "allowed submit should preserve allow proof reason");
}

void denies_submit_when_request_is_not_authenticated() {
  using dasall::access::AccessPolicyGate;
  using dasall::access::PolicyBackendSnapshot;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  AccessPolicyGate gate;
  auto input = make_authenticated_submit_input();
  input.authentication.authenticated = false;

  const auto result = gate.evaluate_submit(input, PolicyBackendSnapshot{});
  assert_true(result.denied(), "unauthenticated submit should be denied fail-closed");
  assert_true(result.reject_reason.has_value(),
              "denied submit should contain explicit reject reason");
  assert_equal(std::string("authentication_required"), *result.reject_reason,
               "unauthenticated submit should map to authentication_required");
}

}  // namespace

int main() {
  try {
    allows_submit_when_policy_backend_emits_allow_proof();
    denies_submit_when_request_is_not_authenticated();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
