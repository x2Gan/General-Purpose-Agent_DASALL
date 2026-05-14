#include <exception>
#include <iostream>
#include <memory>
#include <string>

#include "AccessGatewayFactory.h"
#include "HttpProtocolAdapter.h"
#include "ProtocolAdapterRegistry.h"
#include "support/TestAssertions.h"

namespace {

using dasall::access::AccessDisposition;
using dasall::access::GatewayAccessPipelineOptions;
using dasall::access::ProtocolAdapterRegistry;
using dasall::access::PublishEnvelope;
using dasall::access::RuntimeDispatchRequest;
using dasall::access::RuntimeDispatchResult;
using dasall::access::gateway::HttpProtocolAdapter;
using dasall::access::gateway::HttpRequestContext;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] RuntimeDispatchResult make_completed_result(std::string result_id,
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

void gateway_route_uses_registry_backed_http_adapter_round_trip() {
  auto adapter = std::make_shared<HttpProtocolAdapter>();
  ProtocolAdapterRegistry registry;

  assert_true(registry.register_adapter("gateway.submit.route",
                                        "gateway",
                                        "http_unary",
                                        adapter),
              "registry gateway integration should register http adapter");
  assert_true(registry.resolve_decoder("gateway", "http_unary") == adapter,
              "registry gateway integration should resolve decoder binding");
  assert_true(registry.resolve_encoder({.entry_type = "gateway", .protocol_kind = "http_unary"}) ==
                  adapter,
              "registry gateway integration should resolve encoder binding");

  RuntimeDispatchRequest captured_request;
  int runtime_call_count = 0;

  GatewayAccessPipelineOptions options;
  options.bootstrap_config.allowed_protocols = {"http_unary"};
  options.publish_view.max_payload_bytes = 128;
  options.runtime_dispatch_backend =
      [&captured_request, &runtime_call_count](const RuntimeDispatchRequest& request) {
        captured_request = request;
        ++runtime_call_count;
        return make_completed_result("res-051-registry", "ok");
      };

  auto gateway = dasall::access::create_gateway_access_gateway(std::move(options));
  assert_true(gateway != nullptr,
              "registry gateway integration should build a concrete gateway");
  assert_true(gateway->init(),
              "registry gateway integration should initialize the gateway");

  HttpRequestContext ctx;
  ctx.method = "POST";
  ctx.path = "/v1/submit";
  ctx.headers["content-type"] = "application/json";
  ctx.body =
      R"({"packet_id":"req-051-registry","peer_ref":"jwt:user://tenant-a/alice","payload":"route payload","trace_id":"trace-051-registry","session_hint":"session-051-registry"})";

  const auto response =
      dasall::access::gateway::handle_submit_request(ctx, *gateway, 1024U * 1024U);

  assert_equal(200,
               response.status_code,
               "registry gateway integration should preserve successful response encoding");
  assert_equal(1,
               runtime_call_count,
               "registry gateway integration should reach runtime exactly once");
  assert_equal(std::string("gateway"),
               captured_request.packet.entry_type,
               "registry gateway integration should decode canonical gateway entry type");
  assert_equal(std::string("http_unary"),
               captured_request.packet.protocol_kind,
               "registry gateway integration should decode canonical protocol kind");
}

}  // namespace

int main() {
  try {
    gateway_route_uses_registry_backed_http_adapter_round_trip();
  } catch (const std::exception& ex) {
    std::cerr << "[ProtocolAdapterRegistryGatewayIntegrationTest] FAILED: "
              << ex.what() << '\n';
    return 1;
  }

  return 0;
}