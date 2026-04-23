#include <chrono>
#include <exception>
#include <iostream>

#include "AccessTypes.h"
#include "support/TestAssertions.h"

namespace {

void subject_identity_fields_are_defined() {
  using dasall::access::SubjectIdentity;

  SubjectIdentity subject;
  subject.actor_ref = "mTLS://gateway/client-cert-cn";
  subject.subject_type = "service";
  subject.auth_method = "mTLS";
  subject.trust_level = "authenticated";
  subject.tenant_ref = "tenant-001";

  dasall::tests::support::assert_equal(
      "mTLS://gateway/client-cert-cn",
      subject.actor_ref,
      "actor_ref should be set correctly");
  dasall::tests::support::assert_equal(
      "authenticated",
      subject.trust_level,
      "trust_level should be set correctly");
}

void access_decision_proof_fields_are_defined() {
  using dasall::access::AccessDecisionProof;

  AccessDecisionProof proof;
  proof.decision = "Deny";
  proof.policy_decision_ref = "policy://p1/v1/rule-0";
  proof.reason_code = "INSUFFICIENT_PRIVILEGE";
  proof.reason_description = "User lacks required role";

  dasall::tests::support::assert_equal(
      "Deny",
      proof.decision,
      "decision should be set correctly");
  dasall::tests::support::assert_equal(
      "INSUFFICIENT_PRIVILEGE",
      proof.reason_code,
      "reason_code should be set correctly");
}

void runtime_dispatch_request_includes_sidecars() {
  using dasall::access::RuntimeDispatchRequest;
  using dasall::access::InboundPacket;
  using dasall::access::SubjectIdentity;
  using dasall::access::AccessDecisionProof;

  RuntimeDispatchRequest request;
  request.packet.packet_id = "pkt-001";
  request.subject_identity.actor_ref = "JWT://local/user-id";
  request.subject_identity.trust_level = "trusted";
  request.decision_proof.decision = "Allow";
  request.async_allowed = true;
  request.stream_requested = false;

  dasall::tests::support::assert_equal(
      "pkt-001",
      request.packet.packet_id,
      "packet_id should be accessible");
  dasall::tests::support::assert_equal(
      "JWT://local/user-id",
      request.subject_identity.actor_ref,
      "subject_identity should be accessible");
  dasall::tests::support::assert_true(
      request.async_allowed,
      "async_allowed should be preserved");
}

void publish_envelope_has_expanded_fields() {
  using dasall::access::PublishEnvelope;

  PublishEnvelope envelope;
  envelope.request_id = "req-001";
  envelope.result_id = "res-001";
  envelope.session_id = "sess-001";
  envelope.trace_id = "trace-001";
  envelope.channel_ref = "ch://http/gateway";
  envelope.protocol_kind = "http";
  envelope.protocol_status_hint = "200 OK";
  envelope.is_final = true;

  dasall::tests::support::assert_equal(
      "sess-001",
      envelope.session_id,
      "session_id should be set");
  dasall::tests::support::assert_equal(
      "200 OK",
      envelope.protocol_status_hint,
      "protocol_status_hint should be set");
}

void runtime_dispatch_result_includes_subscription_ref() {
  using dasall::access::RuntimeDispatchResult;
  using dasall::access::AccessDisposition;

  RuntimeDispatchResult result;
  result.disposition = AccessDisposition::StreamAttached;
  result.subscription_ref = "sub-ref-001";

  dasall::tests::support::assert_equal(
      static_cast<int>(AccessDisposition::StreamAttached),
      static_cast<int>(result.disposition),
      "disposition should be StreamAttached");
  dasall::tests::support::assert_true(
      result.subscription_ref.has_value(),
      "subscription_ref should be accessible");
}

}  // namespace

int main() {
  try {
    subject_identity_fields_are_defined();
    access_decision_proof_fields_are_defined();
    runtime_dispatch_request_includes_sidecars();
    publish_envelope_has_expanded_fields();
    runtime_dispatch_result_includes_subscription_ref();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
