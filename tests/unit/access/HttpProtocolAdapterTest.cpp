#include <exception>
#include <iostream>
#include <string>

#include "support/TestAssertions.h"

// 包含被测类（通过 apps/gateway/src include 路径）
#include "HttpProtocolAdapter.h"

namespace {

/// 验证 can_handle("gateway", "http_unary") 返回 true
void test_can_handle_matches_gateway_http_unary() {
  using dasall::access::gateway::HttpProtocolAdapter;
  using dasall::tests::support::assert_true;

  HttpProtocolAdapter adapter;
  assert_true(adapter.can_handle("gateway", "http_unary"),
              "can_handle should match (gateway, http_unary)");
}

/// 验证 can_handle 拒绝非 gateway 类型
void test_can_handle_rejects_daemon() {
  using dasall::access::gateway::HttpProtocolAdapter;
  using dasall::tests::support::assert_true;

  HttpProtocolAdapter adapter;
  assert_true(!adapter.can_handle("daemon", "ipc_uds"),
              "can_handle should reject (daemon, ipc_uds)");
}

/// 验证 can_handle 拒绝非 http_unary 传输
void test_can_handle_rejects_websocket() {
  using dasall::access::gateway::HttpProtocolAdapter;
  using dasall::tests::support::assert_true;

  HttpProtocolAdapter adapter;
  assert_true(!adapter.can_handle("gateway", "websocket"),
              "can_handle should reject (gateway, websocket)");
}

/// 验证 decode 空请求返回空 entry_type（fail-closed）
void test_decode_empty_request_returns_empty_entry_type() {
  using dasall::access::gateway::HttpProtocolAdapter;
  using dasall::access::gateway::HttpRequestContext;
  using dasall::tests::support::assert_true;

  HttpProtocolAdapter adapter;
  // 不注入 active_request — decode 应返回空 InboundPacket
  const auto packet = adapter.decode();
  assert_true(packet.entry_type.empty(),
              "decode without active_request should return empty entry_type");
}

/// 验证 decode 固定 gateway/http_unary，并保留 body 中的业务字段
void test_decode_extracts_entry_type_from_body() {
  using dasall::access::gateway::HttpProtocolAdapter;
  using dasall::access::gateway::HttpRequestContext;
  using dasall::tests::support::assert_true;

  HttpProtocolAdapter adapter;
  HttpRequestContext ctx;
  ctx.method = "POST";
  ctx.path = "/v1/submit";
  ctx.headers["content-type"] = "application/json";
  ctx.body = R"({"entry_type":"task.submit","peer_ref":"actor://test","payload":""})";
  adapter.set_active_request(ctx);

  const auto packet = adapter.decode();
  assert_true(packet.entry_type == "gateway",
              "decode should normalize entry_type to gateway for HTTP submit");
  assert_true(packet.protocol_kind == "http_unary",
              "decode should stamp protocol_kind=http_unary for gateway requests");
}

/// 验证 decode 对无 peer_ref 的请求注入 "http_remote" 默认值
void test_decode_injects_http_remote_peer_ref_when_absent() {
  using dasall::access::gateway::HttpProtocolAdapter;
  using dasall::access::gateway::HttpRequestContext;
  using dasall::tests::support::assert_true;

  HttpProtocolAdapter adapter;
  HttpRequestContext ctx;
  ctx.method = "POST";
  ctx.path = "/v1/submit";
  ctx.headers["content-type"] = "application/json";
  ctx.body = R"({"packet_id":"req-http-default-peer","payload":"ping"})";
  adapter.set_active_request(ctx);

  const auto packet = adapter.decode();
  assert_true(packet.peer_ref == "http_remote",
              "decode should inject 'http_remote' when peer_ref absent");
}

void test_decode_extracts_trace_and_session_metadata() {
  using dasall::access::gateway::HttpProtocolAdapter;
  using dasall::access::gateway::HttpRequestContext;
  using dasall::tests::support::assert_true;

  HttpProtocolAdapter adapter;
  HttpRequestContext ctx;
  ctx.method = "POST";
  ctx.path = "/v1/submit";
  ctx.headers["content-type"] = "application/json";
  ctx.body = R"({"entry_type":"task.submit","trace_id":"trace-gateway-001","session_hint":"session-gateway-001"})";
  adapter.set_active_request(ctx);

  const auto packet = adapter.decode();
  assert_true(packet.trace_id.has_value() && *packet.trace_id == "trace-gateway-001",
              "decode should extract trace_id from JSON body");
  assert_true(packet.session_hint.has_value() && *packet.session_hint == "session-gateway-001",
              "decode should extract session_hint from JSON body");
}

/// 验证 encode 正确填充 HttpResponseContext
void test_encode_fills_response_context() {
  using dasall::access::gateway::HttpProtocolAdapter;
  using dasall::access::gateway::HttpRequestContext;
  using dasall::access::PublishEnvelope;
  using dasall::tests::support::assert_true;

  HttpProtocolAdapter adapter;
  HttpRequestContext ctx;
  ctx.method = "POST";
  ctx.path = "/v1/submit";
  ctx.headers["content-type"] = "application/json";
  ctx.body = R"({"packet_id":"req-encode","payload":"task.submit"})";
  adapter.set_active_request(ctx);

  PublishEnvelope env;
  env.protocol_status_hint = "202";
  env.result_id = "receipt-001";

  const bool ok = adapter.encode(env);
  assert_true(ok, "encode should return true");

  const auto& res = adapter.active_response();
  assert_true(res.status_code == 202, "encode should set HTTP status code 202");
  assert_true(!res.body.empty(), "encode should set response body");
  assert_true(res.body.find("receipt-001") != std::string::npos,
              "encode body should contain result_id");
}

}  // namespace

int main() {
  try {
    test_can_handle_matches_gateway_http_unary();
    test_can_handle_rejects_daemon();
    test_can_handle_rejects_websocket();
    test_decode_empty_request_returns_empty_entry_type();
    test_decode_extracts_entry_type_from_body();
    test_decode_injects_http_remote_peer_ref_when_absent();
    test_decode_extracts_trace_and_session_metadata();
    test_encode_fills_response_context();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
