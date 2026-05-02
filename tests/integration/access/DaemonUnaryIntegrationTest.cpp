#include <exception>
#include <iostream>
#include <string>

#include "DaemonIntegrationHarness.h"

namespace {

using dasall::access::AccessDisposition;
using dasall::access::DaemonAccessPipelineOptions;
using dasall::access::PublishEnvelope;
using dasall::access::RuntimeDispatchRequest;
using dasall::access::RuntimeDispatchResult;
using dasall::tests::integration::access_support::DaemonIntegrationHarness;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

struct RuntimeProbe {
  int calls = 0;
  std::string last_request_id;
  std::string last_actor_ref;
  std::string last_payload;
};

void daemon_unary_happy_path_roundtrip_returns_completed_response() {
  RuntimeProbe probe;

  DaemonAccessPipelineOptions options;
  options.bootstrap_config.allowed_protocols = {"ipc_uds"};
  options.auth_view.trusted_local_subjects = {"local://uid/1000"};
  options.publish_view.max_payload_bytes = 4096;
  options.daemon_profile_id = "daemon.unary.integration";
  options.runtime_dispatch_backend = [&probe](const RuntimeDispatchRequest& request) {
    ++probe.calls;
    probe.last_request_id = request.request_context.at("request_id");
    probe.last_actor_ref = request.subject_identity.actor_ref;
    probe.last_payload = request.packet.payload;

    RuntimeDispatchResult result;
    result.disposition = AccessDisposition::Completed;

    PublishEnvelope envelope;
    envelope.request_id = probe.last_request_id;
    envelope.trace_id = request.request_context.contains("trace_id")
                            ? request.request_context.at("trace_id")
                            : probe.last_request_id + "-trace";
    envelope.protocol_kind = request.packet.protocol_kind;
    envelope.protocol_status_hint = "200";
    envelope.payload = "runtime completed";

    dasall::contracts::AgentResult agent_result;
    agent_result.request_id = probe.last_request_id;
    agent_result.response_text = std::string("runtime completed: ") + probe.last_payload;
    agent_result.task_completed = true;
    envelope.agent_result = std::move(agent_result);

    result.publish_envelope = std::move(envelope);
    return result;
  };

  DaemonIntegrationHarness harness(std::move(options));
  const auto response = harness.make_client().submit("summarize runtime status");

  assert_true(response.ok(),
              "daemon unary integration should receive a parsed daemon response");
  assert_true(response.is_completed(),
              "daemon unary integration should complete happy-path unary request");
  assert_true(response.error_ref == std::nullopt,
              "daemon unary integration should not surface an error_ref on happy path");
  assert_true(response.response_text.has_value(),
              "daemon unary integration should surface runtime response text");
  assert_true(response.response_text->find("runtime completed") != std::string::npos,
              "daemon unary integration should include runtime completion summary");

  assert_equal(1, probe.calls,
               "daemon unary integration should dispatch exactly one runtime request");
  assert_equal(std::string("cli-run"), probe.last_request_id,
               "daemon unary integration should preserve CLI request_id through access core");
  assert_equal(std::string("local://uid/1000"), probe.last_actor_ref,
               "daemon unary integration should resolve trusted local actor_ref");
  assert_equal(std::string("summarize runtime status"), probe.last_payload,
               "daemon unary integration should preserve CLI payload for runtime backend");

  harness.stop();
  assert_true(harness.daemon_stopped_cleanly(),
              "daemon unary integration should stop the daemon thread cleanly");
}

}  // namespace

int main() {
  try {
    daemon_unary_happy_path_roundtrip_returns_completed_response();
  } catch (const std::exception& ex) {
    std::cerr << "[DaemonUnaryIntegrationTest] FAILED: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}