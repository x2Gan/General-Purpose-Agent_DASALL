#include <exception>
#include <iostream>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "AccessErrors.h"
#include "AccessGatewayFactory.h"
#include "IAccessGateway.h"
#include "agent/AgentResult.h"
#include "support/TestAssertions.h"

namespace {

using dasall::access::AccessDisposition;
using dasall::access::AccessErrorCode;
using dasall::access::GatewayAccessPipelineOptions;
using dasall::access::InboundPacket;
using dasall::access::PublishEnvelope;
using dasall::access::RuntimeDispatchRequest;
using dasall::access::RuntimeDispatchResult;
using dasall::contracts::AgentResult;
using dasall::contracts::AgentResultStatus;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

struct CapturedEvent {
  std::string name;
  std::map<std::string, std::string> fields;
};

[[nodiscard]] GatewayAccessPipelineOptions make_base_options(
    std::vector<CapturedEvent>* captured_events) {
  GatewayAccessPipelineOptions options;
  options.bootstrap_config.allowed_protocols = {"http_unary"};
  options.publish_view.max_payload_bytes = 1024;
  options.publish_backend = [](const PublishEnvelope&) {
    return false;
  };
  options.observability_emit_backend =
      [captured_events](const std::string_view event_name,
                        const std::map<std::string, std::string>& fields) {
        captured_events->push_back(CapturedEvent{
            .name = std::string(event_name),
            .fields = fields,
        });
        return true;
      };
  return options;
}

[[nodiscard]] InboundPacket make_packet() {
  InboundPacket packet;
  packet.packet_id = "req-028-publish-failure";
  packet.entry_type = "gateway";
  packet.protocol_kind = "http_unary";
  packet.peer_ref = "jwt:user://tenant-a/alice";
  packet.payload = "run";
  packet.trace_id = std::string("trace-028-publish-failure");
  packet.session_hint = std::string("sess-028-publish-failure");
  return packet;
}

[[nodiscard]] RuntimeDispatchResult make_rejected_runtime_result() {
  RuntimeDispatchResult result;
  result.disposition = AccessDisposition::Rejected;
  result.error_ref = "runtime_rejected_for_publish_audit";

  AgentResult agent_result;
  agent_result.result_id = "res-028-publish-failure";
  agent_result.status = AgentResultStatus::Failed;
  agent_result.result_code = 500;
  agent_result.response_text = "runtime failed";
  agent_result.task_completed = false;
  agent_result.created_at = 1713936000000;

  PublishEnvelope envelope;
  envelope.agent_result = agent_result;
  result.publish_envelope = envelope;
  return result;
}

void publish_failure_emits_audit_event_without_masking_main_result() {
  std::vector<CapturedEvent> captured_events;
  auto options = make_base_options(&captured_events);
  options.runtime_dispatch_backend = [](const RuntimeDispatchRequest&) {
    return make_rejected_runtime_result();
  };

  auto gateway = dasall::access::create_gateway_access_gateway(std::move(options));
  assert_true(gateway != nullptr,
              "publish failure audit integration should build a concrete gateway");
  assert_true(gateway->init(),
              "publish failure audit integration should initialize the gateway");

  const auto result = gateway->submit(make_packet());
  assert_equal(static_cast<int>(AccessDisposition::Rejected),
               static_cast<int>(result.disposition),
               "publish failure audit integration should preserve the rejected main result");
  assert_true(result.error_ref.has_value(),
              "publish failure audit integration should preserve runtime reject reason");
  assert_equal(std::string("runtime_rejected_for_publish_audit"),
               *result.error_ref,
               "publish failure audit integration should not mask the runtime reject reason");
  assert_equal(3,
               static_cast<int>(captured_events.size()),
               "publish failure audit integration should emit request, dispatch, and publish_failed events");
  assert_equal(std::string("access.publish.failed"),
               captured_events[2].name,
               "publish failure audit integration should emit publish_failed last");
  assert_equal(std::to_string(static_cast<int>(AccessErrorCode::PublishChannelUnavailable)),
               captured_events[2].fields.at("error_code"),
               "publish failure audit integration should preserve publish channel unavailable code");
  assert_equal(std::string("result publisher failed to emit publish envelope"),
               captured_events[2].fields.at("detail"),
               "publish failure audit integration should preserve publish failure detail");
  assert_equal(std::string("res-028-publish-failure"),
               captured_events[2].fields.at("result_id"),
               "publish failure audit integration should preserve result_id for audit correlation");
}

}  // namespace

int main() {
  try {
    publish_failure_emits_audit_event_without_masking_main_result();
  } catch (const std::exception& ex) {
    std::cerr << "[AccessPublishFailureAuditTest] FAILED: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}