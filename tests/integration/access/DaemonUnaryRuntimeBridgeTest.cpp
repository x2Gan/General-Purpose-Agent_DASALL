#include <exception>
#include <iostream>
#include <memory>
#include <string>

#include "AccessErrors.h"
#include "AccessGatewayFactory.h"
#include "AgentFacade.h"
#include "ICognitionEngine.h"
#include "IResponseBuilder.h"
#include "support/TestAssertions.h"
#include "../../fixtures/runtime/RuntimeUnaryFixture.h"

namespace {

using dasall::access::AccessDisposition;
using dasall::access::DaemonAccessPipelineOptions;
using dasall::access::InboundPacket;
using dasall::access::RuntimeDispatchRequest;
using dasall::access::RuntimeDispatchResult;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] dasall::contracts::AgentRequest project_agent_request(
    const RuntimeDispatchRequest& request) {
  dasall::contracts::AgentRequest agent_request;
  agent_request.request_id = request.request_context.at("request_id");
  agent_request.session_id = request.request_context.at("session_id");
  agent_request.trace_id = request.request_context.at("trace_id");
  agent_request.user_input = request.packet.payload;
  agent_request.request_channel = dasall::contracts::RequestChannel::Daemon;
  agent_request.created_at = 1710000000000;
  return agent_request;
}

[[nodiscard]] RuntimeDispatchResult map_agent_result_to_dispatch_result(
    const dasall::contracts::AgentResult& agent_result) {
  RuntimeDispatchResult dispatch_result;
  const auto uninitialized_runtime =
      agent_result.status.has_value() &&
      *agent_result.status == dasall::contracts::AgentResultStatus::Failed &&
      agent_result.response_text.has_value() &&
      agent_result.response_text->find("not initialized") != std::string::npos;

  if (uninitialized_runtime) {
    dispatch_result.disposition = AccessDisposition::Rejected;
    dispatch_result.error_ref = "runtime_bridge_unavailable";
    dispatch_result.response_context["error_code"] = std::to_string(
        static_cast<int>(dasall::access::AccessErrorCode::RuntimeBridgeUnavailable));
    return dispatch_result;
  }

  dispatch_result.disposition = AccessDisposition::Completed;
  dispatch_result.publish_envelope = dasall::access::PublishEnvelope{};
  dispatch_result.publish_envelope->protocol_status_hint = "200";
  dispatch_result.publish_envelope->agent_result = agent_result;
  return dispatch_result;
}

[[nodiscard]] InboundPacket make_valid_packet(std::string packet_id) {
  InboundPacket packet;
  packet.packet_id = std::move(packet_id);
  packet.entry_type = "daemon";
  packet.protocol_kind = "ipc_uds";
  packet.peer_ref = "local_trusted:1000";
  packet.payload = "summarize runtime status";
  return packet;
}

void daemon_unary_runtime_bridge_happy_path_calls_agent_facade_handle() {
  auto dependency_set = dasall::tests::runtime_fixture::make_dependency_set();
  dependency_set->cognition_engine =
    std::shared_ptr<dasall::cognition::ICognitionEngine>(
      dasall::cognition::create_cognition_engine());
  dependency_set->response_builder =
    std::shared_ptr<dasall::cognition::IResponseBuilder>(
      dasall::cognition::create_response_builder());

  auto facade = std::make_shared<dasall::runtime::AgentFacade>();
  const auto init_result = facade->init(
    dasall::tests::runtime_fixture::make_init_request(
      "rt-daemon-unary-014",
      "desktop_full",
      "daemon-unary-runtime-bridge",
      std::move(dependency_set)));
  assert_true(init_result.is_ready(),
              "runtime facade should be initialized for daemon unary happy path");

  DaemonAccessPipelineOptions options;
  options.bootstrap_config.allowed_protocols = {"ipc_uds"};
  options.auth_view.trusted_local_subjects = {"local://uid/1000"};
  options.runtime_dispatch_backend =
      [facade](const RuntimeDispatchRequest& request) {
    return map_agent_result_to_dispatch_result(
        facade->handle(project_agent_request(request)));
  };

  auto gateway = dasall::access::create_daemon_access_gateway(std::move(options));
  assert_true(gateway->init(), "daemon gateway should initialize");

  const auto result = gateway->submit(make_valid_packet("req-daemon-unary-ok"));
  assert_equal(static_cast<int>(AccessDisposition::Completed),
               static_cast<int>(result.disposition),
               "initialized runtime should complete unary dispatch via AgentFacade::handle");
  assert_true(result.publish_envelope.has_value(),
              "runtime unary happy path should produce publish envelope");
  assert_true(result.publish_envelope->agent_result.has_value(),
              "publish envelope should carry agent result from AgentFacade");
}

void daemon_unary_runtime_bridge_uninitialized_fails_closed() {
  auto facade = std::make_shared<dasall::runtime::AgentFacade>();

  DaemonAccessPipelineOptions options;
  options.bootstrap_config.allowed_protocols = {"ipc_uds"};
  options.auth_view.trusted_local_subjects = {"local://uid/1000"};
  options.runtime_dispatch_backend =
      [facade](const RuntimeDispatchRequest& request) {
    return map_agent_result_to_dispatch_result(
        facade->handle(project_agent_request(request)));
  };

  auto gateway = dasall::access::create_daemon_access_gateway(std::move(options));
  assert_true(gateway->init(), "daemon gateway should initialize");

  const auto result = gateway->submit(make_valid_packet("req-daemon-unary-uninitialized"));
  assert_equal(static_cast<int>(AccessDisposition::Rejected),
               static_cast<int>(result.disposition),
               "uninitialized runtime should be mapped to fail-closed rejection");
  assert_true(result.error_ref.has_value() && *result.error_ref == "runtime_bridge_unavailable",
              "uninitialized runtime should map to runtime bridge unavailable");
}

}  // namespace

int main() {
  try {
    daemon_unary_runtime_bridge_happy_path_calls_agent_facade_handle();
    daemon_unary_runtime_bridge_uninitialized_fails_closed();
  } catch (const std::exception& ex) {
    std::cerr << "[DaemonUnaryRuntimeBridgeTest] FAILED: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}
