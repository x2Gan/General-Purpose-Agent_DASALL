#include "daemon/DaemonProtocolAdapter.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace dasall::access::daemon {

// ============================================================================
// 内部工具函数 — JSON 最小集解析与序列化
// ============================================================================

namespace {

/// 从 JSON 字符串中提取指定字段的字符串值（简单语义扫描，不处理嵌套）。
/// 若字段不存在返回空字符串。
[[nodiscard]] std::string extract_json_string(std::string_view json,
                                              std::string_view key) {
  // 构造 "key": 搜索模式
  const std::string search_key = std::string("\"") + std::string(key) + "\":";
  const auto pos = json.find(search_key);
  if (pos == std::string_view::npos) {
    return {};
  }

  // 跳过键和可能的空白
  auto start = pos + search_key.size();
  while (start < json.size() && (json[start] == ' ' || json[start] == '\t')) {
    ++start;
  }
  if (start >= json.size()) {
    return {};
  }

  // 字符串值以双引号包围
  if (json[start] != '"') {
    return {};
  }
  ++start;
  const auto end = json.find('"', start);
  if (end == std::string_view::npos) {
    return {};
  }
  return std::string(json.substr(start, end - start));
}

/// 从 JSON 字符串中提取布尔值字段（简单语义扫描）。
[[nodiscard]] bool extract_json_bool(std::string_view json,
                                     std::string_view key,
                                     bool default_value = false) {
  const std::string search_key = std::string("\"") + std::string(key) + "\":";
  const auto pos = json.find(search_key);
  if (pos == std::string_view::npos) {
    return default_value;
  }

  auto start = pos + search_key.size();
  while (start < json.size() && (json[start] == ' ' || json[start] == '\t')) {
    ++start;
  }
  if (start >= json.size()) {
    return default_value;
  }

  // 匹配 true/false 字面量
  if (json.substr(start, 4) == "true") {
    return true;
  }
  if (json.substr(start, 5) == "false") {
    return false;
  }
  return default_value;
}

/// 将 PublishEnvelope 序列化为最小 JSON 响应。
[[nodiscard]] std::string envelope_to_json(const dasall::access::PublishEnvelope& envelope) {
  // 根据 protocol_status_hint 判断整体状态
  const bool is_error =
      !envelope.protocol_status_hint.empty() &&
      (envelope.protocol_status_hint[0] == '4' ||
       envelope.protocol_status_hint[0] == '5');

  std::string json = "{";
  json += "\"status\":\"";
  json += is_error ? "rejected" : "accepted";
  json += "\"";

  if (!envelope.request_id.empty()) {
    json += ",\"request_id\":\"" + envelope.request_id + "\"";
  }
  if (!envelope.result_id.empty()) {
    json += ",\"result_id\":\"" + envelope.result_id + "\"";
  }
  if (!envelope.payload.empty()) {
    json += ",\"result\":\"" + envelope.payload + "\"";
  }
  if (!envelope.protocol_status_hint.empty()) {
    json += ",\"http_status\":\"" + envelope.protocol_status_hint + "\"";
  }

  json += "}";
  return json;
}

}  // namespace

// ============================================================================
// DaemonProtocolAdapter 实现
// ============================================================================

DaemonProtocolAdapter::DaemonProtocolAdapter(
    std::shared_ptr<dasall::platform::IIPC> ipc)
    : ipc_(std::move(ipc)) {}

bool DaemonProtocolAdapter::can_handle(std::string_view entry_type,
                                        std::string_view protocol_kind) const {
  return entry_type == "daemon" && protocol_kind == "ipc_uds";
}

void DaemonProtocolAdapter::set_active_channel(
    dasall::platform::IpcChannelHandle channel,
    std::vector<std::uint8_t> payload) {
  active_channel_ = channel;
  active_payload_ = std::move(payload);
}

InboundPacket DaemonProtocolAdapter::decode() {
  dasall::access::InboundPacket packet;

  if (active_payload_.empty()) {
    return packet;
  }

  // 将原始字节转换为 JSON 字符串
  const std::string_view json_view(
      reinterpret_cast<const char*>(active_payload_.data()),
      active_payload_.size());

  packet.entry_type = "daemon";
  packet.protocol_kind = "ipc_uds";

  // 提取各字段
  packet.packet_id = extract_json_string(json_view, "packet_id");
  packet.payload = extract_json_string(json_view, "payload");
  packet.peer_ref = extract_json_string(json_view, "peer_ref");
  packet.async_preferred = extract_json_bool(json_view, "async_preferred", false);

  // op 字段用于特殊命令处理（ping 等）
  const std::string op = extract_json_string(json_view, "op");
  if (op == "ping") {
    packet.payload = "ping";
    packet.packet_id = "ping";
  }

  return packet;
}

bool DaemonProtocolAdapter::encode(const PublishEnvelope& envelope) {
  if (!ipc_ || !active_channel_.has_consistent_values()) {
    return false;
  }

  const std::string response_json = envelope_to_json(envelope);

  dasall::platform::IpcPayload payload;
  payload.reserve(response_json.size());
  for (const char c : response_json) {
    payload.push_back(static_cast<std::uint8_t>(c));
  }

  const auto result = ipc_->send(active_channel_, payload);
  return result.ok();
}

LocalPeerUidFact DaemonProtocolAdapter::describe_local_peer_uid_fact(
    const dasall::platform::IpcChannelHandle& handle,
    std::string actor_ref) const {
  LocalPeerUidFact fact;
  fact.actor_ref = std::move(actor_ref);

  if (!ipc_) {
    return fact;
  }

  const auto peer_snapshot = ipc_->describe_peer(handle);
  if (!peer_snapshot.ok() || !peer_snapshot.value.has_value()) {
    return fact;
  }

  fact.peer_uid = peer_snapshot.value->peer_uid;
  fact.peer_gid = peer_snapshot.value->peer_gid;
  fact.peer_pid = peer_snapshot.value->peer_pid;
  fact.is_local_socket_peer = peer_snapshot.value->is_local_socket_peer;
  // 安全约束：peer_uid 必须非零（排除 root）才可以 eligible_for_local_trusted
  fact.eligible_for_local_trusted =
      fact.is_local_socket_peer && fact.peer_uid != 0U;

  return fact;
}

}  // namespace dasall::access::daemon
