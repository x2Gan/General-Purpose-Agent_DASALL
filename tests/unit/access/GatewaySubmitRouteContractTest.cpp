#include <exception>
#include <iostream>
#include <string>

#include "AccessGatewayFactory.h"
#include "HttpProtocolAdapter.h"
#include "support/TestAssertions.h"

namespace {

using dasall::access::AccessDisposition;
using dasall::access::GatewayAccessPipelineOptions;
using dasall::access::PublishEnvelope;
using dasall::access::RuntimeDispatchRequest;
using dasall::access::RuntimeDispatchResult;
using dasall::access::gateway::HttpRequestContext;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

RuntimeDispatchResult make_completed_result(std::string result_id,
                                           std::string payload) {
  RuntimeDispatchResult result;
  result.disposition = AccessDisposition::Completed;

  PublishEnvelope envelope;
  envelope.request_id = result_id;
  envelope.result_id = std::move(result_id);
  envelope.protocol_kind = "http_unary";
  envelope.protocol_status_hint = "200";
  envelope.payload = std::move(payload);
  result.publish_envelope = std::move(envelope);
  return result;
}

void test_submit_route_projects_allowlisted_headers_into_runtime_request() {
  RuntimeDispatchRequest captured_request;
  int runtime_call_count = 0;

  GatewayAccessPipelineOptions options;
  options.bootstrap_config.allowed_protocols = {"http_unary"};
  options.publish_view.max_payload_bytes = 128;
  options.runtime_dispatch_backend =
      [&captured_request, &runtime_call_count](const RuntimeDispatchRequest& request) {
        captured_request = request;
        ++runtime_call_count;
        return make_completed_result("res-044-route", "ok");
      };

  auto gateway = dasall::access::create_gateway_access_gateway(std::move(options));
  assert_true(gateway != nullptr, "route contract should create a gateway instance");
  assert_true(gateway->init(), "route contract should initialize the gateway");

  HttpRequestContext ctx;
  ctx.method = "POST";
  ctx.path = "/v1/submit";
  ctx.headers["content-type"] = "application/json";
  ctx.headers["idempotency-key"] = "idem_route_044";
  ctx.body =
      R"({"packet_id":"req-044-route","peer_ref":"jwt:user://tenant-a/alice","payload":"route payload","trace_id":"trace-044-route","session_hint":"session-044-route"})";

  const auto response =
      dasall::access::gateway::handle_submit_request(ctx, *gateway, 1024U * 1024U);

  assert_equal(200,
               response.status_code,
               "route contract should return the encoded completed status");
  assert_equal(1,
               runtime_call_count,
               "valid submit should reach runtime exactly once");
  assert_equal(std::string("gateway"),
               captured_request.packet.entry_type,
               "route contract should normalize entry_type to gateway");
  assert_equal(std::string("http_unary"),
               captured_request.packet.protocol_kind,
               "route contract should normalize protocol_kind to http_unary");
  assert_equal(std::string("idem_route_044"),
               captured_request.request_context.at("idempotency_key"),
               "route contract should project Idempotency-Key into request_context");
  assert_true(captured_request.agent_request.idempotency_key.has_value() &&
                  *captured_request.agent_request.idempotency_key == "idem_route_044",
              "route contract should preserve idempotency key through RequestNormalizer");
}

void test_submit_route_rejects_invalid_idempotency_before_gateway_submit() {
  int runtime_call_count = 0;

  GatewayAccessPipelineOptions options;
  options.bootstrap_config.allowed_protocols = {"http_unary"};
  options.runtime_dispatch_backend =
      [&runtime_call_count](const RuntimeDispatchRequest&) {
        ++runtime_call_count;
        return make_completed_result("res-044-route-invalid", "ok");
      };

  auto gateway = dasall::access::create_gateway_access_gateway(std::move(options));
  assert_true(gateway != nullptr, "invalid route contract should create a gateway instance");
  assert_true(gateway->init(), "invalid route contract should initialize the gateway");

  HttpRequestContext ctx;
  ctx.method = "POST";
  ctx.path = "/v1/submit";
  ctx.headers["content-type"] = "application/json";
  ctx.headers["idempotency-key"] = "bad key";
  ctx.body = R"({"packet_id":"req-044-route-invalid","payload":"route payload"})";

  const auto response =
      dasall::access::gateway::handle_submit_request(ctx, *gateway, 1024U * 1024U);

  assert_equal(400,
               response.status_code,
               "invalid Idempotency-Key should fail closed before gateway submit");
  assert_equal(0,
               runtime_call_count,
               "invalid Idempotency-Key should not reach runtime dispatch");
}

}  // namespace

int main() {
  try {
    test_submit_route_projects_allowlisted_headers_into_runtime_request();
    test_submit_route_rejects_invalid_idempotency_before_gateway_submit();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}