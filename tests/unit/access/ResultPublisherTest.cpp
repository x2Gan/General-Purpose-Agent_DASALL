#include <exception>
#include <iostream>
#include <string>

#include "ResultPublisher.h"
#include "agent/AgentResult.h"
#include "support/TestAssertions.h"

namespace {

[[nodiscard]] dasall::access::RuntimeDispatchRequest make_publish_request() {
  dasall::access::RuntimeDispatchRequest request;
  request.packet.packet_id = "pkt-021-publish";
  request.packet.entry_type = "gateway";
  request.packet.protocol_kind = "http";
  request.request_context["request_id"] = "req-021-publish";
  request.request_context["session_id"] = "sess-021-publish";
  request.request_context["trace_id"] = "trace-021-publish";
  return request;
}

[[nodiscard]] dasall::contracts::AgentResult make_completed_agent_result() {
  dasall::contracts::AgentResult result;
  result.result_id = "res-021-publish";
  result.status = dasall::contracts::AgentResultStatus::Completed;
  result.result_code = 0;
  result.response_text = "ok";
  result.task_completed = true;
  result.created_at = 1713936000000;
  return result;
}

void build_envelope_and_emit_publish_for_completed_result() {
  using dasall::access::ResultPublisher;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  bool emitted = false;
  ResultPublisher publisher(
      [&emitted](const auto& envelope) {
        emitted = true;
        return envelope.request_id == "req-021-publish";
      });

  const auto request = make_publish_request();
  const auto result = make_completed_agent_result();

  const auto envelope = publisher.build_envelope(request, result);
  const bool publish_ok = publisher.emit_publish(envelope);

  assert_true(publish_ok, "completed publish should be emitted successfully");
  assert_true(emitted, "emit backend should be called");
  assert_equal(std::string("res-021-publish"), envelope.result_id,
               "publisher should preserve AgentResult result_id");
  assert_true(envelope.agent_result.has_value(),
              "publish envelope should carry AgentResult fact source");
  assert_equal(std::string("200"), envelope.protocol_status_hint,
               "completed result should map to HTTP 200 semantics");
}

}  // namespace

int main() {
  try {
    build_envelope_and_emit_publish_for_completed_result();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
