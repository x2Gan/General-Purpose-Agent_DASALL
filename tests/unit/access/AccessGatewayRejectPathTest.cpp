#include <chrono>
#include <exception>
#include <iostream>
#include <string>

#include "AccessErrors.h"
#include "AccessGateway.h"
#include "support/TestAssertions.h"

namespace {

void submit_rejects_when_gateway_not_ready_or_shutting_down() {
  using dasall::access::AccessDisposition;
  using dasall::access::AccessErrorCode;
  using dasall::access::AccessGateway;
  using dasall::access::InboundPacket;
  using dasall::access::RuntimeDispatchResult;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  int pipeline_called = 0;
  AccessGateway gateway(
      [&pipeline_called](const InboundPacket&) {
        ++pipeline_called;
        RuntimeDispatchResult result;
        result.disposition = AccessDisposition::Completed;
        return result;
      },
      {});

  assert_true(gateway.init(), "gateway init should succeed");
  gateway.shutdown(std::chrono::milliseconds(1));

  InboundPacket packet;
  packet.packet_id = "pkt-024-reject";

  const auto result = gateway.submit(packet);

  assert_equal(static_cast<int>(AccessDisposition::Rejected),
               static_cast<int>(result.disposition),
               "submit should be rejected after shutdown");
  assert_equal(0, pipeline_called,
               "submit pipeline should not run when gateway is not ready");
  assert_true(result.response_context.find("error_code") != result.response_context.end(),
              "reject path should include error_code in response context");
  assert_equal(std::to_string(static_cast<int>(AccessErrorCode::ShuttingDown)),
               result.response_context.at("error_code"),
               "reject path should expose shutting_down error code");
}

}  // namespace

int main() {
  try {
    submit_rejects_when_gateway_not_ready_or_shutting_down();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
