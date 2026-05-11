/// apps/gateway/src/HealthProbeHandler.h
///
/// DASALL gateway health probe 处理器（v1）
///
/// 职责：
///   - 提供 /health/live、/health/ready、/health/startup 三个探针端点的纯逻辑处理
///   - 不经过 Admission pipeline，不依赖外部服务
///   - 默认返回二值状态；app binary 可追加 runtime readiness 语义标签
///
/// 设计约束（Access 详设 6.20）：
///   - liveness 检查不依赖外部服务，仅检查进程内存活状态
///   - readiness 要求 init() 完成且 started/ready 位均已置位
///   - startup 仅检查 init() 完成位
///   - 不暴露 adapter 列表、registry 大小、queue 深度等内部信息
#pragma once

#include <string>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace dasall::access::gateway {

/// probe 探针响应：HTTP 状态码 + 简洁 body
struct ProbeResult {
  int status_code{200};  ///< 200 = healthy/ready, 503 = not healthy
  std::string body;      ///< "OK"/"FAIL"/"READY"/"NOT_READY"/"STARTED"/"STARTING"
};

/// HTTP 安全头配置（CORS 白名单为空表示禁用 CORS）
struct SecurityConfig {
  std::vector<std::string> cors_allowed_origins;  ///< 空 = CORS 禁用
};

/// HealthProbeHandler — 无 httplib 依赖的纯探针逻辑
///
/// 线程安全声明：set_ready/set_started 应在 init() 完成后、listener 启动前调用；
/// handle_* 方法在 listener 线程中被调用，无互斥——首版为单线程 httplib，可接受。
class HealthProbeHandler {
 public:
  HealthProbeHandler() = default;

  /// 置位"已启动"状态（AccessGateway::init() 成功后调用）
  void set_started(bool started) { started_ = started; }

  /// 置位"已就绪"状态（至少一个 adapter 已注册 + RuntimeBridge 可达）
  void set_ready(bool ready) { ready_ = ready; }

  /// 追加对外 readiness 语义标签，如 runtime_readiness=degraded-ready。
  void set_readiness_detail(std::string detail) {
    readiness_detail_ = std::move(detail);
  }

  /// Liveness probe：进程存活即 200 OK
  [[nodiscard]] ProbeResult handle_live() const;

  /// Readiness probe：init 完成 + ready 已置位 → 200 READY，否则 503 NOT_READY
  [[nodiscard]] ProbeResult handle_ready() const;

  /// Startup probe：init 完成 → 200 STARTED，否则 503 STARTING
  [[nodiscard]] ProbeResult handle_startup() const;

 private:
  bool started_{false};
  bool ready_{false};
  std::optional<std::string> readiness_detail_;
};

/// 将安全头写入 headers map（供 main.cpp 合并到 httplib::Response）
///
/// 始终写入：X-Content-Type-Options, X-Frame-Options, Cache-Control,
///           Content-Security-Policy, X-Request-Id。
/// CORS 仅在 origin 命中 cfg.cors_allowed_origins 白名单时写入。
/// 安全约束：不允许同时使用 "*" 通配符与 Allow-Credentials: true。
void apply_security_headers(
    std::map<std::string, std::string>& headers,
    const SecurityConfig& cfg,
    const std::string& request_origin = "",
    const std::string& request_id = "");

}  // namespace dasall::access::gateway
