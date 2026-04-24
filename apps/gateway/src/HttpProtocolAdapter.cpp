#include "HttpProtocolAdapter.h"

#include <charconv>
#include <cstdint>
#include <string>

namespace dasall::access::gateway {

namespace {

/// 从 JSON 字符串中按 key 提取字符串值（朴素实现，不依赖外部 JSON 库）
std::string extract_json_string(std::string_view json, std::string_view key) {
  // 查找 "key"
  const std::string search_key = "\"" + std::string(key) + "\"";
  const auto key_pos = json.find(search_key);
  if (key_pos == std::string_view::npos) {
    return {};
  }

  // 找到 ':'
  auto colon_pos = json.find(':', key_pos + search_key.size());
  if (colon_pos == std::string_view::npos) {
    return {};
  }

  // 跳过空白
  auto val_start = json.find_first_not_of(" \t\r\n", colon_pos + 1);
  if (val_start == std::string_view::npos || json[val_start] != '"') {
    return {};
  }

  // 读取字符串值（简单扫描，不处理转义）
  ++val_start;
  const auto val_end = json.find('"', val_start);
  if (val_end == std::string_view::npos) {
    return {};
  }

  return std::string(json.substr(val_start, val_end - val_start));
}

/// 将 PublishEnvelope 序列化为 JSON 字符串
std::string envelope_to_json(const dasall::access::PublishEnvelope& env) {
  std::string json = "{";

  auto append_kv = [&](const char* key, const std::string& val,
                        bool comma = true) {
    if (comma) {
      json += ',';
    }
    json += '"';
    json += key;
    json += "\":\"";
    json += val;
    json += '"';
  };

  bool first = true;
  if (!env.result_id.empty()) {
    append_kv("result_id", env.result_id, !first);
    first = false;
  }
  if (!env.protocol_status_hint.empty()) {
    append_kv("status", env.protocol_status_hint, !first);
    first = false;
  }
  if (!env.payload.empty()) {
    append_kv("payload", env.payload, !first);
    first = false;
  }

  json += '}';
  return json;
}

}  // namespace

bool HttpProtocolAdapter::can_handle(std::string_view adapter_id,
                                     std::string_view transport_hint) const {
  return adapter_id == "gateway" && transport_hint == "http_unary";
}

dasall::access::InboundPacket HttpProtocolAdapter::decode() {
  dasall::access::InboundPacket packet;

  if (!active_request_.has_value() ||
      !active_request_->has_consistent_values()) {
    return packet;
  }

  const std::string_view body = active_request_->body;

  // 从 JSON body 提取字段
  packet.packet_id = extract_json_string(body, "packet_id");
  packet.entry_type = extract_json_string(body, "entry_type");
  packet.peer_ref = extract_json_string(body, "peer_ref");
  packet.payload = extract_json_string(body, "payload");

  // peer_ref 默认为 HTTP remote 标记（不是 local trusted）
  if (packet.peer_ref.empty()) {
    packet.peer_ref = "http_remote";
  }

  return packet;
}

bool HttpProtocolAdapter::encode(
    const dasall::access::PublishEnvelope& envelope) {
  active_response_.body = envelope_to_json(envelope);
  active_response_.status_code = hint_to_status_code(
      envelope.protocol_status_hint);
  active_response_.headers["Content-Type"] = "application/json";
  return true;
}

void HttpProtocolAdapter::set_active_request(
    const HttpRequestContext& request) {
  active_request_ = request;
  // 重置上次响应
  active_response_ = HttpResponseContext{};
}

const HttpResponseContext& HttpProtocolAdapter::active_response() const {
  return active_response_;
}

int HttpProtocolAdapter::hint_to_status_code(std::string_view hint) {
  if (hint.empty()) {
    return 200;
  }
  int code = 0;
  const auto* end = hint.data() + hint.size();
  const auto [ptr, ec] = std::from_chars(hint.data(), end, code);
  if (ec != std::errc{} || code < 100 || code > 599) {
    return 500;
  }
  return code;
}

}  // namespace dasall::access::gateway
