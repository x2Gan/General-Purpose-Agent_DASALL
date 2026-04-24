#include <exception>
#include <iostream>
#include <string>

#include "RequestNormalizer.h"
#include "agent/AgentRequest.h"
#include "support/TestAssertions.h"

namespace {

[[nodiscard]] dasall::access::RuntimeDispatchRequest make_identity_projection_request() {
  dasall::access::RuntimeDispatchRequest request;
  request.packet.packet_id = "pkt-019-identity";
  request.packet.entry_type = "gateway";
  request.packet.protocol_kind = "http";
  request.packet.payload = "payload";
  request.subject_identity.actor_ref = "user://tenant-a/operator";
  request.subject_identity.auth_method = "JWT";
  request.subject_identity.trust_level = "trusted";
  request.decision_proof.decision = "Allow";
  request.decision_proof.policy_decision_ref = "policy://access/allow";
  request.request_context["request_id"] = "req-fixed-001";
  request.request_context["session_id"] = "sess-fixed-001";
  request.request_context["trace_id"] = "trace-fixed-001";
  return request;
}

void normalizer_keeps_identity_sidecars_and_channel_projection() {
  using dasall::access::RequestNormalizer;
  using dasall::contracts::RequestChannel;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  RequestNormalizer normalizer;
  const auto output = normalizer.normalize(make_identity_projection_request());

  assert_true(output.normalized, "request should be normalized successfully");
  assert_equal(std::string("user://tenant-a/operator"),
               output.runtime_request.subject_identity.actor_ref,
               "normalizer must preserve subject identity actor_ref");
  assert_equal(std::string("Allow"),
               output.runtime_request.decision_proof.decision,
               "normalizer must preserve decision proof");
  assert_equal(static_cast<int>(RequestChannel::Gateway),
               static_cast<int>(*output.agent_request.request_channel),
               "entry_type gateway should project to RequestChannel::Gateway");
  assert_equal(std::string("req-fixed-001"),
               *output.agent_request.request_id,
               "normalizer should keep provided request_id");
}

}  // namespace

int main() {
  try {
    normalizer_keeps_identity_sidecars_and_channel_projection();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
