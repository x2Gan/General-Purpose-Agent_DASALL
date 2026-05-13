#include <exception>
#include <iostream>
#include <string>

#include "HttpProtocolAdapter.h"
#include "support/TestAssertions.h"

namespace {

using dasall::access::gateway::HttpDecodeErrorCode;
using dasall::access::gateway::HttpProtocolAdapter;
using dasall::access::gateway::HttpRequestContext;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

HttpRequestContext make_base_request() {
  HttpRequestContext ctx;
  ctx.method = "POST";
  ctx.path = "/v1/submit";
  ctx.headers["content-type"] = "application/json";
  ctx.body = R"({"packet_id":"req-044-security","payload":"ok"})";
  return ctx;
}

void test_decode_rejects_nested_payload_shape() {
  HttpProtocolAdapter adapter;
  auto ctx = make_base_request();
  ctx.body = R"({"packet_id":"req-044-security","payload":{"op":"query"}})";

  adapter.set_active_request(ctx);
  const auto packet = adapter.decode();

  assert_true(packet.entry_type.empty(),
              "decode should fail-closed for nested payload objects");
  assert_true(adapter.last_decode_error().has_value(),
              "decode should record a structured error for nested payload objects");
  assert_equal(static_cast<int>(HttpDecodeErrorCode::MalformedJson),
               static_cast<int>(adapter.last_decode_error()->code),
               "nested payload objects should map to malformed_json");
}

void test_decode_rejects_invalid_content_type() {
  HttpProtocolAdapter adapter;
  auto ctx = make_base_request();
  ctx.headers["content-type"] = "text/plain";

  adapter.set_active_request(ctx);
  const auto packet = adapter.decode();

  assert_true(packet.entry_type.empty(),
              "decode should reject non-json content types");
  assert_true(adapter.last_decode_error().has_value(),
              "invalid content type should produce a decode error");
  assert_equal(415,
               adapter.last_decode_error()->status_code,
               "invalid content type should map to HTTP 415");
}

void test_decode_rejects_header_injection() {
  HttpProtocolAdapter adapter;
  auto ctx = make_base_request();
  ctx.headers["x-request-id"] = "req-044\r\nX-Evil: 1";

  adapter.set_active_request(ctx);
  const auto packet = adapter.decode();

  assert_true(packet.entry_type.empty(),
              "decode should reject CRLF header injection attempts");
  assert_true(adapter.last_decode_error().has_value(),
              "header injection should produce a decode error");
  assert_equal(static_cast<int>(HttpDecodeErrorCode::InvalidHeader),
               static_cast<int>(adapter.last_decode_error()->code),
               "header injection should map to invalid_header");
}

void test_decode_rejects_overlimit_body() {
  HttpProtocolAdapter adapter;
  auto ctx = make_base_request();
  ctx.body = R"({"packet_id":"req-044-security","payload":"payload-too-large"})";

  adapter.set_max_request_body_bytes(24U);
  adapter.set_active_request(ctx);
  const auto packet = adapter.decode();

  assert_true(packet.entry_type.empty(),
              "decode should reject bodies over the configured limit");
  assert_true(adapter.last_decode_error().has_value(),
              "overlimit body should produce a decode error");
  assert_equal(413,
               adapter.last_decode_error()->status_code,
               "overlimit body should map to HTTP 413");
}

}  // namespace

int main() {
  try {
    test_decode_rejects_nested_payload_shape();
    test_decode_rejects_invalid_content_type();
    test_decode_rejects_header_injection();
    test_decode_rejects_overlimit_body();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}