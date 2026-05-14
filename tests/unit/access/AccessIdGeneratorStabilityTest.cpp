#include <exception>
#include <iostream>
#include <string>

#include "RequestNormalizer.h"
#include "support/TestAssertions.h"

namespace {

using dasall::access::RequestNormalizer;
using dasall::access::RuntimeDispatchRequest;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] RuntimeDispatchRequest make_request() {
  RuntimeDispatchRequest request;
  request.packet.packet_id = "req-051-stable-id";
  request.packet.entry_type = "gateway";
  request.packet.protocol_kind = "http_unary";
  request.packet.payload = "hello";
  request.subject_identity.actor_ref = "user://tenant-a/alice";
  return request;
}

void generated_ids_are_stable_across_normalizer_instances() {
  RequestNormalizer first;
  RequestNormalizer second;

  const auto first_output = first.normalize(make_request());
  const auto second_output = second.normalize(make_request());

  assert_true(first_output.normalized && second_output.normalized,
              "stable id test requires successful normalization");
  assert_equal(first_output.agent_request.request_id.value_or(std::string()),
               second_output.agent_request.request_id.value_or(std::string()),
               "request_id should be stable across normalizer instances");
  assert_equal(first_output.agent_request.session_id.value_or(std::string()),
               second_output.agent_request.session_id.value_or(std::string()),
               "session_id should be stable across normalizer instances");
  assert_equal(first_output.agent_request.trace_id.value_or(std::string()),
               second_output.agent_request.trace_id.value_or(std::string()),
               "trace_id should be stable across normalizer instances");
  assert_true(first_output.agent_request.request_id !=
                  first_output.agent_request.session_id.value_or(std::string()),
              "request_id and session_id should remain distinct semantic IDs");
  assert_true(first_output.agent_request.request_id !=
                  first_output.agent_request.trace_id.value_or(std::string()),
              "request_id and trace_id should remain distinct semantic IDs");
}

}  // namespace

int main() {
  try {
    generated_ids_are_stable_across_normalizer_instances();
  } catch (const std::exception& ex) {
    std::cerr << "[AccessIdGeneratorStabilityTest] FAILED: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}