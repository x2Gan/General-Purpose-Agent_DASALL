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
  ctx.body = R"({"entry_type":"task.submit"})";
  adapter.set_active_request(ctx);

  PublishEnvelope env;
  env.protocol_status_hint = "400";
  env.payload = "admission rejected";

    (void)adapter.encode(env);
  assert_true(adapter.active_response().status_code == 400,
              "encode with 400 hint should set response status to 400");
}

/// 验证 decode 不感知 HTTP method（仅用 body）
void test_decode_parses_body_regardless_of_method() {
  using dasall::access::gateway::HttpProtocolAdapter;
  using dasall::access::gateway::HttpRequestContext;
  using dasall::tests::support::assert_true;

  HttpProtocolAdapter adapter;
  HttpRequestContext ctx;
  ctx.method = "GET";  // 非 POST，body 仍能解析
  ctx.path = "/v1/submit";
  ctx.body = R"({"entry_type":"task.query","payload":"q"})";
  adapter.set_active_request(ctx);

  const auto packet = adapter.decode();
  assert_true(packet.entry_type == "task.query",
              "decode should extract entry_type regardless of HTTP method");
  assert_true(packet.payload == "q",
              "decode should extract payload field");
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
    test_decode_parses_body_regardless_of_method();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
