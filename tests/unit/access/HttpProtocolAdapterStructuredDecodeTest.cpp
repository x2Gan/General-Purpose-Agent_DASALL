#include <exception>
#include <iostream>
#include <string>

#include "HttpProtocolAdapter.h"
#include "support/TestAssertions.h"

namespace {

void test_decode_structures_gateway_submit_payload() {
  using dasall::access::gateway::HttpProtocolAdapter;
  using dasall::access::gateway::HttpRequestContext;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  HttpProtocolAdapter adapter;
  HttpRequestContext ctx;
  ctx.method = "POST";
  ctx.path = "/v1/submit";
  ctx.body =
      R"({"packet_id":"req-044-structured","entry_type":"task.submit","peer_ref":"jwt:user://tenant-a/alice","payload":"hello \"gateway\"","trace_id":"trace-044-structured","session_hint":"session-044-structured","async_preferred":true,"stream_requested":false})";
  ctx.headers["content-type"] = "application/json; charset=utf-8";
  ctx.headers["idempotency-key"] = "idem_044_structured";
  adapter.set_active_request(ctx);

  const auto packet = adapter.decode();

  assert_equal(std::string("req-044-structured"),
               packet.packet_id,
               "decode should preserve packet_id from JSON body");
  assert_equal(std::string("gateway"),
               packet.entry_type,
               "decode should hard-code entry_type=gateway for HTTP submit");
  assert_equal(std::string("http_unary"),
               packet.protocol_kind,
               "decode should hard-code protocol_kind=http_unary for HTTP submit");
  assert_equal(std::string("jwt:user://tenant-a/alice"),
               packet.peer_ref,
               "decode should preserve peer_ref when body provides one");
  assert_equal(std::string("hello \"gateway\""),
               packet.payload,
               "decode should parse escaped JSON string payloads without truncation");
  assert_true(packet.trace_id.has_value() && *packet.trace_id == "trace-044-structured",
              "decode should preserve trace_id when provided");
  assert_true(packet.session_hint.has_value() &&
                  *packet.session_hint == "session-044-structured",
              "decode should preserve session_hint when provided");
  assert_true(packet.async_preferred,
              "decode should parse async_preferred=true from structured JSON");
  assert_true(!packet.stream_requested,
              "decode should parse stream_requested=false from structured JSON");
  assert_true(packet.headers.count("idempotency_key") > 0U,
              "decode should project allowlisted headers into packet headers");
  assert_equal(std::string("idem_044_structured"),
               packet.headers.at("idempotency_key"),
               "decode should canonicalize Idempotency-Key into packet headers");
}

}  // namespace

int main() {
  try {
    test_decode_structures_gateway_submit_payload();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}