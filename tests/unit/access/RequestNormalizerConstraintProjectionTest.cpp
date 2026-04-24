#include <exception>
#include <iostream>
#include <string>

#include "RequestNormalizer.h"
#include "support/TestAssertions.h"

namespace {

[[nodiscard]] dasall::access::RuntimeDispatchRequest make_constraint_request() {
  dasall::access::RuntimeDispatchRequest request;
  request.packet.packet_id = "pkt-019-constraint";
  request.packet.entry_type = "daemon";
  request.packet.protocol_kind = "ipc";
  request.packet.payload = "diagnostic payload";
  request.subject_identity.actor_ref = "service://daemon/local";
  request.decision_proof.decision = "Allow";
  request.request_context["constraint_set"] = "safety=high;network=deny";
  request.request_context["internal_secret"] = "must_not_project_to_contract";
  return request;
}

void normalizer_projects_only_constraint_whitelist() {
  using dasall::access::RequestNormalizer;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  RequestNormalizer normalizer;
  const auto output = normalizer.normalize(make_constraint_request());

  assert_true(output.normalized, "request should be normalized successfully");
  assert_true(output.agent_request.constraint_set.has_value(),
              "constraint_set should be projected to AgentRequest");
  assert_equal(std::string("safety=high;network=deny"),
               *output.agent_request.constraint_set,
               "constraint_set projection should preserve original value");
  assert_true(!output.agent_request.domain_context.has_value(),
              "non-whitelist request_context fields should not leak into AgentRequest");
  assert_true(output.runtime_request.request_context.contains("constraint_set"),
              "constraint_set should stay available in runtime sidecar context");
}

}  // namespace

int main() {
  try {
    normalizer_projects_only_constraint_whitelist();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
