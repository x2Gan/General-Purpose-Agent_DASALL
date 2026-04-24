/// apps/simulator/src/SimulatorProtocolAdapter.cpp
///
/// DASALL simulator protocol adapter 实现
#include "SimulatorProtocolAdapter.h"

#include <cstdint>

namespace dasall::access::simulator {

bool SimulatorProtocolAdapter::can_handle(std::string_view entry_type,
                                          std::string_view protocol_kind) const {
  // 仅处理 simulator 入口的 deterministic_test 协议
  return entry_type == "simulator" && protocol_kind == "deterministic_test";
}

dasall::access::InboundPacket SimulatorProtocolAdapter::decode() {
  // 实现简化版的 JSON 解析（生产环境可用 nlohmann/json）
  // fixture 格式：{ "entry_type": "...", "request_id": "...", "payload": {...} }

  dasall::access::InboundPacket packet;
  packet.peer_ref = "simulator_local";
  packet.packet_id = subject_.actor_ref;  // 将 fixture actor_ref 作为 packet_id

  // 从 request_body 中提取关键字段
  if (request_body_.empty()) {
    packet.entry_type = "simulator";
    return packet;  // 空请求默认降级为无操作
  }

  // 简化的字段提取（生产环境应使用正式 JSON parser）
  size_t entry_type_pos = request_body_.find("\"entry_type\":");
  if (entry_type_pos != std::string::npos) {
    size_t val_start = request_body_.find('\"', entry_type_pos + 13);
    size_t val_end = request_body_.find('\"', val_start + 1);
    if (val_start != std::string::npos && val_end != std::string::npos) {
      packet.entry_type = request_body_.substr(val_start + 1, val_end - val_start - 1);
    }
  }

  return packet;
}

bool SimulatorProtocolAdapter::encode(const dasall::access::PublishEnvelope& envelope) {
  // v1 版本：simulator 不执行实际 runtime
  // 直接返回 pending 状态与 result_id
  response_body_ = R"({
    "status": "accepted",
    "result_id": ")" + envelope.result_id + R"(",
    "session_id": ")" + envelope.session_id + R"(",
    "status_code": 202
  })";
  return true;
}

void SimulatorProtocolAdapter::set_active_request(const std::string& request_body) {
  request_body_ = request_body;
  response_body_.clear();
}

}  // namespace dasall::access::simulator
