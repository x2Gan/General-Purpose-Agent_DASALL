#include <exception>
#include <iostream>
#include <string>

#include "RequestNormalizer.h"
#include "support/TestAssertions.h"

namespace {

[[nodiscard]] dasall::access::RuntimeDispatchRequest make_request_without_trace_ids() {
  dasall::access::RuntimeDispatchRequest request;
  request.packet.packet_id = "pkt-019-001";
  request.packet.entry_type = "gateway";
  request.packet.protocol_kind = "http";
  request.packet.payload = "hello";
  request.subject_identity.actor_ref = "user://tenant-a/alice";
  request.decision_proof.decision = "Allow";
  request.request_context["idempotency_key"] = "idem-019";
  return request;
}

void normalizer_generates_missing_trace_ids_and_marks_ready() {
  using dasall::access::RequestNormalizer;
  using dasall::tests::support::assert_true;

  RequestNormalizer normalizer;
  const auto output = normalizer.normalize(make_request_without_trace_ids());

  assert_true(output.normalized, "request should be normalized successfully");
  assert_true(output.runtime_request.request_context.contains("request_id"),
              "normalize should generate request_id when absent");
  assert_true(output.runtime_request.request_context.contains("session_id"),
              "normalize should generate session_id when absent");
  assert_true(output.runtime_request.request_context.contains("trace_id"),
              "normalize should generate trace_id when absent");
  assert_true(output.runtime_request.request_context.at("normalizer_ready") == "true",
              "normalize should emit normalizer_ready marker");
  assert_true(!output.agent_request.request_id->empty(),
              "projected AgentRequest request_id should be non-empty");
}

}  // namespace

int main() {
  try {
    normalizer_generates_missing_trace_ids_and_marks_ready();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
