/// apps/gateway/src/main.cpp
///
/// DASALL gateway HTTP 入口组合根（v1: unary + accepted async receipt）
///
/// 职责：
///   - 使用 cpp-httplib 创建 HTTP/1.1 服务器
///   - POST /v1/submit → HttpProtocolAdapter.decode() → IAccessGateway.submit()
///     → HttpProtocolAdapter.encode() → HTTP response
///   - 所有请求经 AccessGateway 主链（Admission/Policy/Normalizer/RuntimeBridge）
///
/// 架构约束：
///   - v1 仅支持 unary POST + accepted async receipt（无 WebSocket/MQTT）
///   - 健康探针路径 /health/* 在 028 实现后集成
///   - gateway 不持有 runtime 内部控制权（ADR-007/008 边界）
///   - ACC-TODO-028：/health/live、/health/ready、/health/startup 已集成
///   - 所有响应写入固定安全头（X-Content-Type-Options 等）
///   - CORS 默认禁用，通过 SecurityConfig::cors_allowed_origins 白名单启用

#include <csignal>
#include <chrono>
#include <iostream>
#include <map>
#include <memory>
#include <string>

// cpp-httplib：header-only HTTP/1.1 库
#include "httplib.h"

#include "AccessGatewayFactory.h"
#include "HttpProtocolAdapter.h"
#include "IAccessGateway.h"
#include "HealthProbeHandler.h"

namespace {

/// 全局运行标志，用于信号处理器
std::atomic<bool> g_stop_requested{false};
httplib::Server* g_server = nullptr;

extern "C" void on_shutdown_signal(int /*signal*/) {
  g_stop_requested.store(true);
  if (g_server != nullptr) {
    g_server->stop();
  }
}

}  // namespace

int main() {
  dasall::access::gateway::SecurityConfig sec_cfg;

  dasall::access::GatewayAccessPipelineOptions gateway_options;
  gateway_options.bootstrap_config.entry_type = "gateway";
  gateway_options.bootstrap_config.allowed_protocols = {"http"};
  gateway_options.publish_view.cors_allowed_origins = sec_cfg.cors_allowed_origins;

  auto gateway = dasall::access::create_gateway_access_gateway(
      std::move(gateway_options));
  if (!gateway->init()) {
    std::cerr << "[dasall_gateway] AccessGateway init failed: production submit pipeline unavailable\n";
    return 1;
  }

  // 2. 注册信号处理器
  std::signal(SIGTERM, on_shutdown_signal);
  std::signal(SIGINT, on_shutdown_signal);

  // 3. 创建 httplib 服务器
  httplib::Server srv;
  g_server = &srv;

  // 4. POST /v1/submit — HTTP unary 请求入口
  // 健康探针处理器
  dasall::access::gateway::HealthProbeHandler health;
  health.set_started(true);
  health.set_ready(gateway->is_ready());

  /// 辅助：将安全头合并到 httplib::Response
  auto apply_sec = [&](httplib::Response& res,
                       const std::string& origin = "",
                       const std::string& req_id = "") {
    std::map<std::string, std::string> h;
    dasall::access::gateway::apply_security_headers(h, sec_cfg, origin, req_id);
    for (const auto& [k, v] : h) {
      res.set_header(k.c_str(), v.c_str());
    }
  };

  // 4a. GET /health/live
  srv.Get("/health/live", [&health, &apply_sec](const httplib::Request& req,
                                                 httplib::Response& res) {
    const auto r = health.handle_live();
    res.status = r.status_code;
    res.set_content(r.body, "text/plain");
    apply_sec(res, req.get_header_value("Origin"));
  });

  // 4b. GET /health/ready
  srv.Get("/health/ready", [&health, &apply_sec](const httplib::Request& req,
                                                  httplib::Response& res) {
    const auto r = health.handle_ready();
    res.status = r.status_code;
    res.set_content(r.body, "text/plain");
    apply_sec(res, req.get_header_value("Origin"));
  });

  // 4c. GET /health/startup
  srv.Get("/health/startup", [&health, &apply_sec](const httplib::Request& req,
                                                    httplib::Response& res) {
    const auto r = health.handle_startup();
    res.status = r.status_code;
    res.set_content(r.body, "text/plain");
    apply_sec(res, req.get_header_value("Origin"));
  });

  // 4d. OPTIONS preflight — 不经过 Admission，直接回应 204
  srv.Options(".*", [&apply_sec](const httplib::Request& req,
                                  httplib::Response& res) {
    res.status = 204;
    apply_sec(res, req.get_header_value("Origin"));
  });

  // 4e. POST /v1/submit — HTTP unary 请求入口
  srv.Post("/v1/submit", [&gateway, &apply_sec](const httplib::Request& req,
                                                 httplib::Response& res) {
    dasall::access::gateway::HttpProtocolAdapter adapter;

    dasall::access::gateway::HttpRequestContext ctx;
    ctx.method = req.method;
    ctx.path = req.path;
    ctx.body = req.body;
    for (const auto& [k, v] : req.headers) {
      ctx.headers[k] = v;
    }

    adapter.set_active_request(ctx);
    auto packet = adapter.decode();

    const std::string origin = req.get_header_value("Origin");
    const std::string req_id = req.get_header_value("X-Request-Id");

    if (packet.entry_type.empty()) {
      res.status = 400;
      res.set_content(R"({"error":"invalid request body"})", "application/json");
      apply_sec(res, origin, req_id);
      return;
    }

    const auto result = gateway->submit(packet);

    if (result.publish_envelope.has_value()) {
      (void)adapter.encode(*result.publish_envelope);
    } else {
      dasall::access::PublishEnvelope fallback;
      fallback.protocol_status_hint =
          (result.disposition == dasall::access::AccessDisposition::AcceptedAsync)
              ? "202"
              : "400";
      fallback.result_id = result.receipt_ref.value_or("");
      fallback.payload = result.error_ref.value_or("");
      (void)adapter.encode(fallback);
    }

    const auto& http_res = adapter.active_response();
    res.status = http_res.status_code;
    res.set_content(http_res.body, "application/json");
    apply_sec(res, origin, req_id);
  });

  // 5. 启动监听
  const int port = 8080;
  std::cout << "[dasall_gateway] listening on :" << port << "\n";
  srv.listen("0.0.0.0", port);

  // 6. 优雅关闭
  gateway->shutdown(std::chrono::milliseconds(5000));
  std::cout << "[dasall_gateway] stopped\n";
  return 0;
}
