#include <chrono>
#include <exception>
#include <iostream>
#include <string>

#include "AccessGateway.h"
#include "support/TestAssertions.h"

namespace {

void submit_pipeline_and_publish_backend_work_in_ready_state() {
  using dasall::access::AccessDisposition;
  using dasall::access::AccessGateway;
  using dasall::access::InboundPacket;
  using dasall::access::PublishEnvelope;
  using dasall::access::RuntimeDispatchResult;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  bool published = false;
  AccessGateway gateway(
      [](const InboundPacket& packet) {
        RuntimeDispatchResult result;
        result.disposition = AccessDisposition::Completed;
        result.response_context["packet_id"] = packet.packet_id;
        return result;
      },
      [&published](const PublishEnvelope& envelope) {
        published = (envelope.request_id == "req-024-facade");
        return published;
      });

  assert_true(gateway.init(), "gateway init should succeed");

  InboundPacket packet;
  packet.packet_id = "pkt-024-facade";
  packet.entry_type = "gateway";
  packet.protocol_kind = "http";

  const auto dispatch_result = gateway.submit(packet);
  assert_equal(static_cast<int>(AccessDisposition::Completed),
               static_cast<int>(dispatch_result.disposition),
               "submit should run configured pipeline in ready state");

  PublishEnvelope envelope;
  envelope.request_id = "req-024-facade";
  assert_true(gateway.publish_result(envelope),
              "publish_result should call backend in ready state");
  assert_true(published, "publish backend should be executed");
}

}  // namespace

int main() {
  try {
    submit_pipeline_and_publish_backend_work_in_ready_state();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
