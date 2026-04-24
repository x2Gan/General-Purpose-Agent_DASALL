/// tests/unit/access/AccessHttpSecurityHeadersTest.cpp
///
/// ACC-TODO-028：HTTP 安全头与 CORS 单元测试
///
/// 覆盖：
///   - apply_security_headers 写入所有必须安全头
///   - CORS 默认禁用（cors_allowed_origins 为空）
///   - origin 命中白名单时写入 CORS 头
///   - origin 不在白名单时不写入 CORS 头
///   - 通配符 "*" 被 fail-closed 拒绝（不写入 CORS 头）
///   - X-Request-Id 在有 request_id 时写入

#include <cassert>
#include <map>
#include <string>
#include <vector>

#include "HealthProbeHandler.h"
#include "support/TestAssertions.h"

namespace {

using dasall::access::gateway::SecurityConfig;
using dasall::access::gateway::apply_security_headers;
using dasall::tests::support::assert_true;

/// 固定安全头：X-Content-Type-Options, X-Frame-Options, Cache-Control, CSP
void test_security_headers_set_all_required_fields() {
  SecurityConfig cfg;  // CORS 禁用
  std::map<std::string, std::string> h;
  apply_security_headers(h, cfg);

  assert_true(h.count("X-Content-Type-Options") > 0,
              "X-Content-Type-Options must be present");
  assert_true(h.at("X-Content-Type-Options") == "nosniff",
              "X-Content-Type-Options must be nosniff");

  assert_true(h.count("X-Frame-Options") > 0,
              "X-Frame-Options must be present");
  assert_true(h.at("X-Frame-Options") == "DENY",
              "X-Frame-Options must be DENY");

  assert_true(h.count("Cache-Control") > 0,
              "Cache-Control must be present");
  assert_true(h.at("Cache-Control") == "no-store",
              "Cache-Control must be no-store");

  assert_true(h.count("Content-Security-Policy") > 0,
              "Content-Security-Policy must be present");
  assert_true(h.at("Content-Security-Policy") == "default-src 'none'",
              "Content-Security-Policy must be default-src 'none'");
}

/// CORS 默认禁用：cors_allowed_origins 为空时不写入 Access-Control 头
void test_cors_disabled_by_default() {
  SecurityConfig cfg;  // empty cors_allowed_origins
  std::map<std::string, std::string> h;
  apply_security_headers(h, cfg, "https://example.com");

  assert_true(h.count("Access-Control-Allow-Origin") == 0,
              "Access-Control-Allow-Origin must not be present when CORS disabled");
}

/// CORS：origin 命中白名单时写入 Access-Control-Allow-Origin
void test_cors_headers_added_for_allowed_origin() {
  SecurityConfig cfg;
  cfg.cors_allowed_origins = {"https://allowed.example.com"};
  std::map<std::string, std::string> h;
  apply_security_headers(h, cfg, "https://allowed.example.com");

  assert_true(h.count("Access-Control-Allow-Origin") > 0,
              "Access-Control-Allow-Origin must be present for allowed origin");
  assert_true(h.at("Access-Control-Allow-Origin") == "https://allowed.example.com",
              "Access-Control-Allow-Origin must match the allowed origin");
}

/// CORS：origin 不在白名单时不写入 CORS 头
void test_cors_rejected_for_unknown_origin() {
  SecurityConfig cfg;
  cfg.cors_allowed_origins = {"https://allowed.example.com"};
  std::map<std::string, std::string> h;
  apply_security_headers(h, cfg, "https://evil.example.com");

  assert_true(h.count("Access-Control-Allow-Origin") == 0,
              "Access-Control-Allow-Origin must not be present for unknown origin");
}

/// 安全约束：通配符 "*" 被 fail-closed 拒绝
void test_cors_wildcard_rejected_fail_closed() {
  SecurityConfig cfg;
  cfg.cors_allowed_origins = {"*"};  // 不允许
  std::map<std::string, std::string> h;
  apply_security_headers(h, cfg, "https://any.example.com");

  assert_true(h.count("Access-Control-Allow-Origin") == 0,
              "Wildcard '*' in cors_allowed_origins must be rejected fail-closed");
}

/// X-Request-Id 在有 request_id 时写入
void test_request_id_header_written_when_provided() {
  SecurityConfig cfg;
  std::map<std::string, std::string> h;
  apply_security_headers(h, cfg, "", "req-12345");

  assert_true(h.count("X-Request-Id") > 0,
              "X-Request-Id must be present when request_id provided");
  assert_true(h.at("X-Request-Id") == "req-12345",
              "X-Request-Id value must match request_id");
}

/// X-Request-Id 在 request_id 为空时不写入
void test_request_id_not_written_when_empty() {
  SecurityConfig cfg;
  std::map<std::string, std::string> h;
  apply_security_headers(h, cfg, "", "");

  assert_true(h.count("X-Request-Id") == 0,
              "X-Request-Id must not be present when request_id is empty");
}

}  // namespace

int main() {
  test_security_headers_set_all_required_fields();
  test_cors_disabled_by_default();
  test_cors_headers_added_for_allowed_origin();
  test_cors_rejected_for_unknown_origin();
  test_cors_wildcard_rejected_fail_closed();
  test_request_id_header_written_when_provided();
  test_request_id_not_written_when_empty();
  return 0;
}
