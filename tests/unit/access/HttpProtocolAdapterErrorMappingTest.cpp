#include <exception>
#include <iostream>
#include <string>

#include "support/TestAssertions.h"
#include "HttpProtocolAdapter.h"

namespace {

/// 验证 hint_to_status_code 正确转换常见状态码
void test_hint_to_status_code_converts_200() {
  using dasall::access::gateway::HttpProtocolAdapter;
  using dasall::tests::support::assert_true;

  assert_true(HttpProtocolAdapter::hint_to_status_code("200") == 200,
              "hint_to_status_code should return 200 for '200'");
}

void test_hint_to_status_code_converts_202() {
  using dasall::access::gateway::HttpProtocolAdapter;
  using dasall::tests::support::assert_true;

  assert_true(HttpProtocolAdapter::hint_to_status_code("202") == 202,
              "hint_to_status_code should return 202 for '202'");
}

void test_hint_to_status_code_converts_400() {
  using dasall::access::gateway::HttpProtocolAdapter;
  using dasall::tests::support::assert_true;

  assert_true(HttpProtocolAdapter::hint_to_status_code("400") == 400,
              "hint_to_status_code should return 400 for '400'");
}

/// 验证 hint_to_status_code 对空字符串返回 200
void test_hint_to_status_code_empty_returns_200() {
  using dasall::access::gateway::HttpProtocolAdapter;
  using dasall::tests::support::assert_true;

  assert_true(HttpProtocolAdapter::hint_to_status_code("") == 200,
              "hint_to_status_code should return 200 for empty hint");
}

/// 验证 hint_to_status_code 对无效字符串返回 500
void test_hint_to_status_code_invalid_returns_500() {
  using dasall::access::gateway::HttpProtocolAdapter;
  using dasall::tests::support::assert_true;

  assert_true(HttpProtocolAdapter::hint_to_status_code("not-a-code") == 500,
              "hint_to_status_code should return 500 for invalid hint");
}

/// 验证 encode 400 时响应状态码为 400
void test_encode_rejected_envelope_returns_400() {
  using dasall::access::gateway::HttpProtocolAdapter;
  using dasall::access::gateway::HttpRequestContext;
  using dasall::access::PublishEnvelope;
  using dasall::tests::support::assert_true;

  HttpProtocolAdapter adapter;
  HttpRequestContext ctx;
  ctx.method = "POST";
  ctx.path = "/v1/submit";
  ctx.headers["content-type"] = "application/json";
  ctx.body = R"({"packet_id":"req-error-mapping","payload":"task.submit"})";
  adapter.set_active_request(ctx);

  PublishEnvelope env;
  env.protocol_status_hint = "400";
  env.payload = "admission rejected";

    (void)adapter.encode(env);
  assert_true(adapter.active_response().status_code == 400,
              "encode with 400 hint should set response status to 400");
}

/// 验证 decode 对非法 method fail-closed
void test_decode_rejects_invalid_method() {
  using dasall::access::gateway::HttpProtocolAdapter;
  using dasall::access::gateway::HttpRequestContext;
  using dasall::tests::support::assert_true;

  HttpProtocolAdapter adapter;
  HttpRequestContext ctx;
  ctx.method = "GET";
  ctx.path = "/v1/submit";
  ctx.headers["content-type"] = "application/json";
  ctx.body = R"({"packet_id":"req-invalid-method","payload":"q"})";
  adapter.set_active_request(ctx);

  const auto packet = adapter.decode();
  assert_true(packet.entry_type.empty(),
              "decode should fail closed for non-POST submit methods");
  assert_true(adapter.last_decode_error().has_value() &&
                  adapter.last_decode_error()->status_code == 405,
              "decode should surface 405 when method is not POST");
}

}  // namespace

int main() {
  try {
    test_hint_to_status_code_converts_200();
    test_hint_to_status_code_converts_202();
    test_hint_to_status_code_converts_400();
    test_hint_to_status_code_empty_returns_200();
    test_hint_to_status_code_invalid_returns_500();
    test_encode_rejected_envelope_returns_400();
    test_decode_rejects_invalid_method();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
