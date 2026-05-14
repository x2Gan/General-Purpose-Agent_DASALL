#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>

#include "AccessPolicyGate.h"
#include "support/TestAssertions.h"

namespace {

class RecordingPolicyEvaluator final : public dasall::access::IAccessPolicyEvaluator {
 public:
  dasall::access::AccessPolicyEvaluationResult next_result{
      .allowed = true,
      .requires_confirmation = false,
      .decision_proof = dasall::access::AccessDecisionProof{
          .decision = "Allow",
          .policy_decision_ref = "policy://access/test",
          .reason_code = "allow_proof",
          .reason_description = std::string("recorded evaluator allowed request"),
          .evaluated_at = std::string("policy-eval"),
      },
      .reject_reason = std::nullopt,
  };

  mutable std::optional<dasall::access::AccessPolicyQuery> last_query;

  [[nodiscard]] dasall::access::AccessPolicyEvaluationResult evaluate(
      const dasall::access::AccessPolicyQuery& query) const override {
    last_query = query;
    return next_result;
  }
};

[[nodiscard]] dasall::access::AccessPolicyEvaluationInput make_submit_input() {
  dasall::access::AccessPolicyEvaluationInput input;
  input.authentication.authenticated = true;
  input.authentication.subject_identity.actor_ref = "user://tenant-a/alice";
  input.authentication.subject_identity.auth_method = "JWT";
  input.authentication.subject_identity.subject_type = "user";
  input.packet.packet_id = "pkt-policy-attr-001";
  input.packet.entry_type = "gateway";
  input.packet.protocol_kind = "http_unary";
  input.packet.payload = "{}";
  input.packet.session_hint = std::string("sess-policy-attr-001");
  input.packet.trace_id = std::string("trace-policy-attr-001");
  input.snapshot_fingerprint = dasall::access::SnapshotVersionFingerprint{
      .bootstrap_revision = "gateway-bootstrap:desktop_full",
      .effective_profile_id = "desktop_full",
      .runtime_policy_generation = 42,
  };
  return input;
}

void seam_query_contains_subject_channel_environment_operation_target_and_fingerprint() {
  using dasall::access::AccessPolicyGate;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto evaluator = std::make_shared<RecordingPolicyEvaluator>();
  AccessPolicyGate gate(evaluator);

  const auto result = gate.evaluate_submit(make_submit_input());
  assert_true(result.allowed,
              "custom evaluator should allow the submit request in the focused seam test");
  assert_true(evaluator->last_query.has_value(),
              "custom evaluator should receive an access policy query");

  const auto& query = *evaluator->last_query;
  assert_equal(std::string("user://tenant-a/alice"),
               query.subject_identity.actor_ref,
               "policy query should preserve subject actor_ref");
  assert_equal(std::string("gateway"),
               query.entry_type,
               "policy query should preserve entry_type as channel metadata");
  assert_equal(std::string("http_unary"),
               query.protocol_kind,
               "policy query should preserve protocol_kind as channel metadata");
  assert_equal(std::string("submit"),
               query.operation_target.operation,
               "policy query should preserve operation");
  assert_equal(std::string("entry"),
               query.operation_target.target_type,
               "policy query should preserve target_type");
  assert_equal(std::string("gateway"),
               query.operation_target.target_ref,
               "policy query should preserve target_ref");
  assert_equal(std::string("pkt-policy-attr-001"),
               query.request_id,
               "policy query should preserve request_id");
  assert_equal(std::string("sess-policy-attr-001"),
               query.session_id,
               "policy query should preserve session_id");
  assert_equal(std::string("trace-policy-attr-001"),
               query.trace_id,
               "policy query should preserve trace_id");
  assert_true(query.snapshot_fingerprint.has_value(),
              "policy query should preserve snapshot fingerprint when available");
  assert_equal(std::string("gateway-bootstrap:desktop_full"),
               query.snapshot_fingerprint->bootstrap_revision,
               "policy query should preserve bootstrap revision fingerprint");
  assert_equal(std::string("desktop_full"),
               query.snapshot_fingerprint->effective_profile_id,
               "policy query should preserve effective profile id fingerprint");
  assert_equal(42,
               static_cast<int>(query.snapshot_fingerprint->runtime_policy_generation),
               "policy query should preserve runtime policy generation fingerprint");
  assert_true(!query.sensitive_request,
              "submit seam query should not be marked sensitive by default");
}

}  // namespace

int main() {
  try {
    seam_query_contains_subject_channel_environment_operation_target_and_fingerprint();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}