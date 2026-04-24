#include <exception>
#include <iostream>
#include <string>

#include "ResultPublisher.h"
#include "agent/AgentResult.h"
#include "support/TestAssertions.h"

namespace {

[[nodiscard]] dasall::access::RuntimeDispatchRequest make_failure_publish_request() {
  dasall::access::RuntimeDispatchRequest request;
  request.packet.packet_id = "pkt-021-channel-failure";
  request.packet.entry_type = "gateway";
  request.packet.protocol_kind = "http";
  request.request_context["request_id"] = "req-021-channel-failure";
  request.request_context["session_id"] = "sess-021-channel-failure";
  request.request_context["trace_id"] = "trace-021-channel-failure";
  return request;
}

[[nodiscard]] dasall::contracts::AgentResult make_failed_agent_result() {
  dasall::contracts::AgentResult result;
  result.result_id = "res-021-channel-failure";
  result.status = dasall::contracts::AgentResultStatus::Failed;
  result.result_code = 500;
  result.response_text = "runtime failed";
  result.task_completed = false;
  result.created_at = 1713936000000;
  return result;
}

void returns_publish_channel_unavailable_when_emit_fails() {
  using dasall::access::AccessErrorCode;
  using dasall::access::ResultPublisher;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  ResultPublisher publisher(
      [](const auto&) {
        return false;
      });

  const auto attempt = publisher.publish(
      make_failure_publish_request(),
      make_failed_agent_result());

  assert_true(!attempt.published,
              "publish attempt should fail when channel backend rejects envelope");
  assert_true(attempt.error.has_value(),
              "channel failure should emit explicit access error");
  assert_equal(static_cast<int>(AccessErrorCode::PublishChannelUnavailable),
               static_cast<int>(attempt.error->code),
               "channel failure should map to PublishChannelUnavailable");
}

}  // namespace

int main() {
  try {
    returns_publish_channel_unavailable_when_emit_fails();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
