#include <exception>
#include <iostream>
#include <string>

#include "RequestNormalizer.h"
#include "agent/AgentRequestGuards.h"
#include "support/TestAssertions.h"

namespace {

[[nodiscard]] dasall::access::RuntimeDispatchRequest make_contract_compat_request() {
  dasall::access::RuntimeDispatchRequest request;
  request.packet.packet_id = "pkt-019-contract";
  request.packet.entry_type = "cli";
  request.packet.protocol_kind = "ipc";
  request.packet.payload = "run diagnostics";
  request.subject_identity.actor_ref = "user://tenant-a/devops";
  request.decision_proof.decision = "Allow";
  request.request_context["request_id"] = "req-contract-001";
  request.request_context["session_id"] = "sess-contract-001";
  request.request_context["trace_id"] = "trace-contract-001";
  return request;
}

void normalized_agent_request_passes_contract_guards() {
  using dasall::access::RequestNormalizer;
  using dasall::contracts::validate_agent_request_boundary;
  using dasall::contracts::validate_agent_request_field_rules;
  using dasall::tests::support::assert_true;

  RequestNormalizer normalizer;
  const auto output = normalizer.normalize(make_contract_compat_request());

  assert_true(output.normalized, "request should be normalized successfully");

  const auto boundary_result = validate_agent_request_boundary(output.agent_request);
  assert_true(boundary_result.ok,
              "projected AgentRequest should satisfy boundary compatibility guard");

  const auto field_result = validate_agent_request_field_rules(output.agent_request);
  assert_true(field_result.ok,
              "projected AgentRequest should satisfy field compatibility guard");
}

}  // namespace

int main() {
  try {
    normalized_agent_request_passes_contract_guards();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
