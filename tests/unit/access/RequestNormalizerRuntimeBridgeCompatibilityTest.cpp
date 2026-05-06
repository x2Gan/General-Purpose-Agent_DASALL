#include <exception>
#include <iostream>
#include <string>

#include "RequestNormalizer.h"
#include "RuntimeBridge.h"
#include "support/TestAssertions.h"

namespace {

[[nodiscard]] dasall::access::RuntimeDispatchRequest make_compat_request() {
  dasall::access::RuntimeDispatchRequest request;
  request.packet.packet_id = "pkt-027-compat";
  request.packet.entry_type = "gateway";
  request.packet.protocol_kind = "http";
  request.packet.payload = "run";
  request.subject_identity.actor_ref = "user://tenant-a/alice";
  request.decision_proof.decision = "Allow";
  request.request_context["request_id"] = "req-027-compat";
  request.request_context["session_id"] = "sess-027-compat";
  request.request_context["trace_id"] = "trace-027-compat";
  request.request_context["idempotency_key"] = "idem-027-compat";
  return request;
}

void normalized_runtime_request_remains_bridge_compatible_after_sidecar_ids_drop() {
  using dasall::access::AccessDisposition;
  using dasall::access::RequestNormalizer;
  using dasall::access::RuntimeBridge;
  using dasall::access::RuntimeDispatchResult;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  RequestNormalizer normalizer;
  const auto normalized = normalizer.normalize(make_compat_request());
  assert_true(normalized.normalized,
              "request should normalize before compatibility handoff check");

  auto handoff_request = normalized.runtime_request;
  handoff_request.request_context.erase("request_id");
  handoff_request.request_context.erase("session_id");
  handoff_request.request_context.erase("trace_id");

  RuntimeBridge bridge(
      [](const auto&) {
        RuntimeDispatchResult result;
        result.disposition = AccessDisposition::Completed;
        return result;
      },
      {});

  const auto mapped = bridge.dispatch(handoff_request);

  assert_equal(normalized.agent_request.request_id.value_or(std::string()),
               mapped.response_context.at("request_id"),
               "runtime bridge should preserve request_id from public handoff even if sidecar id is absent");
  assert_equal(normalized.agent_request.session_id.value_or(std::string()),
               mapped.response_context.at("session_id"),
               "runtime bridge should preserve session_id from public handoff even if sidecar id is absent");
  assert_equal(normalized.agent_request.trace_id.value_or(std::string()),
               mapped.response_context.at("trace_id"),
               "runtime bridge should preserve trace_id from public handoff even if sidecar id is absent");
}

}  // namespace

int main() {
  try {
    normalized_runtime_request_remains_bridge_compatible_after_sidecar_ids_drop();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}