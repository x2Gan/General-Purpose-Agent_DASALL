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

#include <csignal>
#include <iostream>
#include <memory>

// cpp-httplib：header-only HTTP/1.1 库
#include "httplib.h"

#include "HttpProtocolAdapter.h"
#include "IAccessGateway.h"
#include "linux/UnixIpcProvider.h"

// AccessGateway concrete class（通过 access/src PRIVATE include）
#include "AccessGateway.h"

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
  // 1. 构造 AccessGateway
  auto gateway = std::make_shared<dasall::access::AccessGateway>();
  if (!gateway->init()) {
    std::cerr << "[dasall_gateway] AccessGateway init failed\n";
    return 1;
  }

  // 2. 注册信号处理器
  std::signal(SIGTERM, on_shutdown_signal);
  std::signal(SIGINT, on_shutdown_signal);

  // 3. 创建 httplib 服务器
  httplib::Server srv;
  g_server = &srv;

  // 4. POST /v1/submit — HTTP unary 请求入口
  srv.Post("/v1/submit", [&gateway](const httplib::Request& req,
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

    if (packet.entry_type.empty()) {
      res.status = 400;
      res.set_content(R"({"error":"invalid request body"})", "application/json");
      return;
    }

    const auto result = gateway->submit(packet);

    if (result.publish_envelope.has_value()) {
      adapter.encode(*result.publish_envelope);
    } else {
      dasall::access::PublishEnvelope fallback;
      fallback.protocol_status_hint =
          (result.disposition == dasall::access::AccessDisposition::AcceptedAsync)
              ? "202"
              : "400";
      fallback.result_id = result.receipt_ref.value_or("");
      fallback.payload = result.error_ref.value_or("");
      adapter.encode(fallback);
    }

    const auto& http_res = adapter.active_response();
    res.status = http_res.status_code;
    res.set_content(http_res.body, "application/json");
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
