#include <exception>
#include <iostream>
#include <string>

#include "AccessPolicyGate.h"
#include "support/TestAssertions.h"

namespace {

[[nodiscard]] dasall::access::AccessPolicyEvaluationInput make_authenticated_override_input() {
  dasall::access::AccessPolicyEvaluationInput input;
  input.authentication.authenticated = true;
  input.authentication.subject_identity.actor_ref = "operator://ops/local";
  input.authentication.subject_identity.auth_method = "local_trusted";
  input.packet.packet_id = "pkt-override-001";
  input.packet.entry_type = "daemon";
  input.packet.protocol_kind = "uds";
  input.packet.payload = "[{\"op\":\"replace\",\"path\":\"/runtime\"}]";
  return input;
}

void denies_override_when_source_fact_is_invalid() {
  using dasall::access::AccessPolicyGate;
  using dasall::access::OverrideSourceFact;
  using dasall::access::PolicyBackendSnapshot;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  AccessPolicyGate gate;
  OverrideSourceFact source_fact;
  source_fact.source_type = "external_http";
  source_fact.has_config_patch_metadata = true;
  source_fact.path_op_summary_complete = true;

  const auto result = gate.evaluate_override_request(
      make_authenticated_override_input(), source_fact, PolicyBackendSnapshot{});
  assert_true(result.denied(), "override should deny when source is not allowlisted");
  assert_true(result.reject_reason.has_value(), "deny result should expose reject reason");
  assert_equal(std::string("override_source_invalid"), *result.reject_reason,
               "invalid source should map to override_source_invalid");
}

void returns_confirmation_when_backend_requires_sensitive_confirmation() {
  using dasall::access::AccessPolicyGate;
  using dasall::access::OverrideSourceFact;
  using dasall::access::PolicyBackendSnapshot;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  AccessPolicyGate gate;
  OverrideSourceFact source_fact;
  source_fact.source_type = "ops_command";
  source_fact.has_config_patch_metadata = true;
  source_fact.path_op_summary_complete = true;
  source_fact.ttl_valid = true;
  source_fact.target_ref_present = true;

  PolicyBackendSnapshot backend;
  backend.allow_override = false;
  backend.require_confirmation_for_override = true;
  backend.decision_ref = "policy://access/require-confirmation";

  const auto result = gate.evaluate_override_request(
      make_authenticated_override_input(), source_fact, backend);
  assert_true(result.requires_confirmation,
              "sensitive override should emit confirmation-required result");
  assert_equal(std::string("RequireConfirmation"), result.decision_proof.decision,
               "confirmation path should emit RequireConfirmation decision");
  assert_equal(std::string("confirmation_required"), result.decision_proof.reason_code,
               "confirmation path should preserve confirmation reason code");
}

}  // namespace

int main() {
  try {
    denies_override_when_source_fact_is_invalid();
    returns_confirmation_when_backend_requires_sensitive_confirmation();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
