#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "AccessTypes.h"
#include "IProtocolAdapter.h"

namespace dasall::access::gateway {

/// HTTP 请求数据（由 apps/gateway/main.cpp 从 httplib 注入）
struct HttpRequestContext {
  /// HTTP 方法（"POST" / "GET"）
  std::string method;

  /// 请求路径（"/v1/submit" / "/v1/task/..." 等）
  std::string path;

  /// 请求体（原始字节，通常为 JSON）
  std::string body;

  /// 请求头（lowercase key → value）
  std::unordered_map<std::string, std::string> headers;

  [[nodiscard]] bool has_consistent_values() const {
    return !method.empty() && !path.empty();
  }
};

/// HTTP 响应数据（由 encode() 填充，由 main.cpp 注入到 httplib 响应）
struct HttpResponseContext {
  /// HTTP 状态码（200 / 202 / 400 / 500 等）
  int status_code = 200;

  /// 响应体（JSON）
  std::string body;

  /// 响应头
  std::unordered_map<std::string, std::string> headers;
};

/// HttpProtocolAdapter — IProtocolAdapter HTTP 实现
///
/// 职责：
///   1. can_handle("gateway", "http_unary") 匹配 HTTP unary 请求
///   2. set_active_request() 注入当前 HTTP 请求上下文
///   3. decode() 将 HTTP 请求 → InboundPacket（从 JSON body 提取字段）
///   4. encode() 将 PublishEnvelope → HttpResponseContext
///
/// 边界约束：
///   - 不持有 TCP 连接或 httplib 实例；仅做协议翻译
///   - 无状态单实例：每次 set_active_request() 建立单次请求上下文
///   - v1 仅支持 POST /v1/submit（unary）；streaming 路径延后到 A5
///
class HttpProtocolAdapter : public dasall::access::IProtocolAdapter {
 public:
  /// 实现 IProtocolAdapter::can_handle()。
  /// 仅接受 adapter_id="gateway"，transport_hint="http_unary"。
  [[nodiscard]] bool can_handle(std::string_view adapter_id,
                                std::string_view transport_hint) const override;

  /// 实现 IProtocolAdapter::decode()。
  /// 从 active_request_ JSON body 解析出 InboundPacket。
  [[nodiscard]] dasall::access::InboundPacket decode() override;

  /// 实现 IProtocolAdapter::encode()。
  /// 将 PublishEnvelope 序列化为 JSON，并填充 active_response_。
  /// @return 是否成功序列化
  [[nodiscard]] bool encode(
      const dasall::access::PublishEnvelope& envelope) override;

  /// 注入当前 HTTP 请求上下文（每次处理一个请求前调用）。
  void set_active_request(const HttpRequestContext& request);

  /// 获取最近一次 encode() 填充的响应上下文。
  [[nodiscard]] const HttpResponseContext& active_response() const;

  /// 将 protocol_status_hint 字符串转换为 HTTP 状态码整数。
  /// 用于 encode() 内部和调用方状态码映射。
  [[nodiscard]] static int hint_to_status_code(std::string_view hint);

 private:
  std::optional<HttpRequestContext> active_request_;
  HttpResponseContext active_response_;
};

}  // namespace dasall::access::gateway
