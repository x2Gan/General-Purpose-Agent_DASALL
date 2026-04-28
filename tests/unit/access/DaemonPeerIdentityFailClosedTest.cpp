#include <exception>
#include <iostream>
#include <string>

#include "AccessPolicyGate.h"
#include "support/TestAssertions.h"

namespace {

[[nodiscard]] dasall::access::AccessPolicyEvaluationInput make_daemon_override_input(
    std::string auth_method,
    std::string actor_ref) {
  dasall::access::AccessPolicyEvaluationInput input;
  input.authentication.authenticated = true;
  input.authentication.subject_identity.auth_method = std::move(auth_method);
  input.authentication.subject_identity.actor_ref = std::move(actor_ref);
  input.packet.packet_id = "pkt-dmd-012";
  input.packet.entry_type = "daemon";
  input.packet.protocol_kind = "ipc_uds";
  input.packet.payload = "[{\"op\":\"replace\",\"path\":\"/runtime\"}]";
  return input;
}

[[nodiscard]] dasall::access::OverrideSourceFact make_valid_override_source() {
  dasall::access::OverrideSourceFact source;
  source.source_type = "ops_command";
  source.has_config_patch_metadata = true;
  source.path_op_summary_complete = true;
  source.ttl_valid = true;
  source.target_ref_present = true;
  return source;
}

void denies_daemon_privileged_override_without_local_trusted_identity() {
  using dasall::access::AccessPolicyGate;
  using dasall::access::PolicyBackendSnapshot;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  AccessPolicyGate gate;
  PolicyBackendSnapshot backend;
  backend.allow_override = true;
  backend.decision_ref = "policy://access/allow-override";

  const auto result = gate.evaluate_override_request(
      make_daemon_override_input("JWT", "user://tenant-a/alice"),
      make_valid_override_source(),
      backend);

  assert_true(result.denied(),
              "daemon privileged override must fail closed when peer identity is not local_trusted");
  assert_true(result.reject_reason.has_value(),
              "daemon fail-closed path should expose reject reason");
  assert_equal(std::string("daemon_peer_identity_required"), *result.reject_reason,
               "daemon privileged override should require local peer identity");
}

void allows_daemon_privileged_override_with_local_trusted_identity() {
  using dasall::access::AccessPolicyGate;
  using dasall::access::PolicyBackendSnapshot;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  AccessPolicyGate gate;
  PolicyBackendSnapshot backend;
  backend.allow_override = true;
  backend.decision_ref = "policy://access/allow-override";

  const auto result = gate.evaluate_override_request(
      make_daemon_override_input("local_trusted", "local://uid/1000"),
      make_valid_override_source(),
      backend);

  assert_true(result.allowed,
              "daemon privileged override should be allowed when local peer identity is trusted");
  assert_equal(std::string("Allow"), result.decision_proof.decision,
               "allow path should emit explicit allow decision");
}

void denies_daemon_privileged_override_when_local_trusted_actor_ref_is_missing() {
  using dasall::access::AccessPolicyGate;
  using dasall::access::PolicyBackendSnapshot;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  AccessPolicyGate gate;
  PolicyBackendSnapshot backend;
  backend.allow_override = true;

  const auto result = gate.evaluate_override_request(
      make_daemon_override_input("local_trusted", ""),
      make_valid_override_source(),
      backend);

  assert_true(result.denied(),
              "daemon privileged override must reject local_trusted subjects without stable actor_ref");
  assert_equal(std::string("daemon_peer_identity_required"), *result.reject_reason,
               "missing local peer actor_ref should map to daemon peer identity required");
}

}  // namespace

int main() {
  try {
    denies_daemon_privileged_override_without_local_trusted_identity();
    allows_daemon_privileged_override_with_local_trusted_identity();
    denies_daemon_privileged_override_when_local_trusted_actor_ref_is_missing();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
