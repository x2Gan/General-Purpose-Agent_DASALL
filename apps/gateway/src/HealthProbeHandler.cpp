/// apps/gateway/src/HealthProbeHandler.cpp
///
/// HealthProbeHandler 实现 + apply_security_headers

#include "HealthProbeHandler.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace dasall::access::gateway {

ProbeResult HealthProbeHandler::handle_live() const {
  // liveness：进程存活即可，不依赖外部服务
  return ProbeResult{200, "OK"};
}

ProbeResult HealthProbeHandler::handle_ready() const {
  if (started_ && ready_) {
    return ProbeResult{200, "READY"};
  }
  return ProbeResult{503, "NOT_READY"};
}

ProbeResult HealthProbeHandler::handle_startup() const {
  if (started_) {
    return ProbeResult{200, "STARTED"};
  }
  return ProbeResult{503, "STARTING"};
}

void apply_security_headers(
    std::map<std::string, std::string>& headers,
    const SecurityConfig& cfg,
    const std::string& request_origin,
    const std::string& request_id) {
  // 固定安全头（始终写入，无论请求来源）
  headers["X-Content-Type-Options"] = "nosniff";
  headers["X-Frame-Options"] = "DENY";
  headers["Cache-Control"] = "no-store";
  headers["Content-Security-Policy"] = "default-src 'none'";

  if (!request_id.empty()) {
    headers["X-Request-Id"] = request_id;
  }

  // CORS：仅在白名单非空且 origin 命中时写入
  if (cfg.cors_allowed_origins.empty()) {
    return;  // CORS 禁用
  }

  // 安全约束：不允许通配符 "*" 出现在非空白名单中（已明确 deny-by-default）
  const bool has_wildcard = std::find(
      cfg.cors_allowed_origins.begin(),
      cfg.cors_allowed_origins.end(), "*") != cfg.cors_allowed_origins.end();
  if (has_wildcard) {
    // 通配符拒绝：fail-closed，不写入 CORS 头
    return;
  }

  // 检查 origin 是否命中白名单
  if (request_origin.empty()) {
    return;  // 无 origin，跳过 CORS
  }

  const bool origin_allowed = std::find(
      cfg.cors_allowed_origins.begin(),
      cfg.cors_allowed_origins.end(),
      request_origin) != cfg.cors_allowed_origins.end();

  if (!origin_allowed) {
    return;  // origin 未在白名单，不添加 CORS 头
  }

  headers["Access-Control-Allow-Origin"] = request_origin;
  headers["Access-Control-Allow-Methods"] = "POST, GET, OPTIONS";
  headers["Access-Control-Allow-Headers"] = "Content-Type, Idempotency-Key";
  headers["Vary"] = "Origin";
}

}  // namespace dasall::access::gateway
