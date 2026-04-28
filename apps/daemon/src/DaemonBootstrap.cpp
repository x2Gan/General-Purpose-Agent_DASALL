#include "DaemonBootstrap.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <utility>

namespace dasall::apps::daemon {

DaemonBootstrap::DaemonBootstrap(
    std::shared_ptr<dasall::platform::IIPC> ipc,
    std::shared_ptr<dasall::access::IAccessGateway> gateway)
    : ipc_(std::move(ipc)),
      gateway_(std::move(gateway)),
      listener_host_(ipc_) {}

bool DaemonBootstrap::run(const dasall::platform::IpcEndpoint& endpoint) {
  if (!ipc_ || !gateway_) {
    return false;
  }

  if (!lifecycle_.start()) {
    return false;
  }

  if (!gateway_->is_ready()) {
    (void)lifecycle_.mark_failed();
    return false;
  }

  if (!lifecycle_.mark_binding()) {
    (void)lifecycle_.mark_failed();
    return false;
  }

  const auto bind_result = listener_host_.bind(endpoint);
  if (!bind_result.ok()) {
    (void)lifecycle_.mark_failed();
    return false;
  }

  listener_host_.set_connection_handler(
      [this](const dasall::platform::IpcChannelHandle& channel) {
        return handle_connection(channel);
      });

  if (!lifecycle_.mark_ready()) {
    (void)listener_host_.close();
    (void)lifecycle_.mark_failed();
    return false;
  }

  const auto loop_result =
      listener_host_.accept_loop(stop_requested_, kAcceptDeadlineMs);
  (void)listener_host_.close();
  if (!loop_result.ok() || !loop_result.value.value_or(false)) {
    (void)lifecycle_.mark_failed();
    return false;
  }

  return true;
}

void DaemonBootstrap::stop() {
  stop_requested_.store(true);
  (void)lifecycle_.shutdown(std::chrono::milliseconds::zero());
}

bool DaemonBootstrap::handle_connection(
    const dasall::platform::IpcChannelHandle& channel) {
  if (!channel.has_consistent_values()) {
    return false;
  }

  // 接收请求 payload
  const auto recv_result = ipc_->receive(channel, kReceiveDeadlineMs);
  if (!recv_result.ok()) {
    return false;
  }
  if (!recv_result.value.has_value() || recv_result.value->peer_closed) {
    return false;
  }

  const auto& raw_payload = recv_result.value->data;
  if (raw_payload.empty()) {
    return false;
  }

  // 构造 DaemonProtocolAdapter 并注入连接上下文
  dasall::access::daemon::DaemonProtocolAdapter adapter(ipc_);
  adapter.set_active_channel(channel, raw_payload);

  // 获取 peer identity（用于 local trusted 判定）
  const auto peer_fact =
      adapter.describe_local_peer_uid_fact(channel, "actor://daemon/local");

  // 解码请求 -> InboundPacket
  auto packet = adapter.decode();
  if (packet.entry_type.empty()) {
    return false;
  }

  // 将 peer identity 注入 packet.peer_ref（以 "local_trusted:" 前缀标记）
  if (peer_fact.eligible_for_local_trusted) {
    packet.peer_ref = "local_trusted:" + std::to_string(peer_fact.peer_uid);
  } else {
    // 非信任来源：保持原 peer_ref 或标记为 untrusted
    if (packet.peer_ref.empty()) {
      packet.peer_ref = "untrusted";
    }
  }

  // 处理 ping 特殊命令（绕过 AccessGateway 直接响应）
  if (packet.packet_id == "ping") {
    const std::string pong = make_ping_response();
    dasall::platform::IpcPayload pong_payload;
    pong_payload.reserve(pong.size());
    for (const char c : pong) {
      pong_payload.push_back(static_cast<std::uint8_t>(c));
    }
    const auto send_result = ipc_->send(channel, pong_payload);
    return send_result.ok();
  }

  // 通过 IAccessGateway 主链处理请求
  const auto dispatch_result = gateway_->submit(packet);

  // 若有 PublishEnvelope 则直接编码发回
  if (dispatch_result.publish_envelope.has_value()) {
    return adapter.encode(*dispatch_result.publish_envelope);
  }

  // 异步接受：生成最小 accepted 响应
  if (dispatch_result.disposition == dasall::access::AccessDisposition::AcceptedAsync) {
    dasall::access::PublishEnvelope async_envelope;
    async_envelope.protocol_status_hint = "202";
    async_envelope.result_id =
        dispatch_result.receipt_ref.value_or("async-receipt");
    return adapter.encode(async_envelope);
  }

  // 拒绝路径：生成 rejected 响应
  dasall::access::PublishEnvelope reject_envelope;
  reject_envelope.protocol_status_hint = "400";
  reject_envelope.payload =
      dispatch_result.error_ref.value_or("rejected");
  return adapter.encode(reject_envelope);
}

std::string DaemonBootstrap::make_ping_response() {
  return R"({"status":"ok","service":"dasall-daemon"})";
}

}  // namespace dasall::apps::daemon
