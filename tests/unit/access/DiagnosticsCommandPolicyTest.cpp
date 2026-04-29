#include <exception>
#include <iostream>
#include <string>

#include "AccessPolicyGate.h"
#include "support/TestAssertions.h"

namespace {

[[nodiscard]] dasall::access::AccessPolicyEvaluationInput make_diag_input(
    const std::string& actor_ref,
    const std::string& auth_method) {
  dasall::access::AccessPolicyEvaluationInput input;
  input.authentication.authenticated = true;
  input.authentication.subject_identity.actor_ref = actor_ref;
  input.authentication.subject_identity.auth_method = auth_method;
  input.packet.packet_id = "diag-pkt-020";
  input.packet.entry_type = "daemon";
  input.packet.protocol_kind = "ipc_uds";
  input.packet.payload = "command_name=health.snapshot";
  return input;
}

void diagnostics_policy_denies_without_local_trusted_subject() {
  using dasall::access::AccessPolicyGate;
  using dasall::access::PolicyBackendSnapshot;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  AccessPolicyGate gate;
  PolicyBackendSnapshot backend;
  backend.allow_diagnostics = true;

  const auto result = gate.evaluate_diagnostics_request(
      make_diag_input("operator://ops/local", "token"),
      "health.snapshot",
      backend);

  assert_true(result.denied(),
              "diagnostics policy should fail closed without local_trusted daemon subject");
  assert_equal(std::string("daemon_peer_identity_required"), *result.reject_reason,
               "diagnostics policy should preserve daemon peer identity deny reason");
}

void diagnostics_policy_allows_local_trusted_whitelisted_request() {
  using dasall::access::AccessPolicyGate;
  using dasall::access::PolicyBackendSnapshot;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  AccessPolicyGate gate;
  PolicyBackendSnapshot backend;
  backend.allow_diagnostics = true;
  backend.decision_ref = "policy://access/diag-allow";

  const auto result = gate.evaluate_diagnostics_request(
      make_diag_input("local://uid/1000", "local_trusted"),
      "health.snapshot",
      backend);

  assert_true(result.allowed,
              "diagnostics policy should allow local trusted diagnostics requests when backend allows it");
  assert_equal(std::string("policy://access/diag-allow"),
               result.decision_proof.policy_decision_ref,
               "diagnostics policy should preserve decision ref for allowed path");
}

}  // namespace

int main() {
  try {
    diagnostics_policy_denies_without_local_trusted_subject();
    diagnostics_policy_allows_local_trusted_whitelisted_request();
  } catch (const std::exception& ex) {
    std::cerr << "[DiagnosticsCommandPolicyTest] FAILED: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}
