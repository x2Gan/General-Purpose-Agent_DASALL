#include "daemon/DaemonProtocolAdapter.h"

#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>

#include "daemon/DaemonFrameCodec.h"

namespace dasall::access::daemon {

namespace {

[[nodiscard]] UdsResponseDisposition map_disposition(const PublishEnvelope& envelope) {
  if (!envelope.protocol_status_hint.empty()) {
    const int status = std::atoi(envelope.protocol_status_hint.c_str());
    if (status == 202) {
      return UdsResponseDisposition::AcceptedAsync;
    }
    if (status == 503) {
      return UdsResponseDisposition::NotReady;
    }
    if (status >= 200 && status < 300) {
      return UdsResponseDisposition::Completed;
    }
  }

  return UdsResponseDisposition::Rejected;
}

[[nodiscard]] UdsResponseFrame build_response_frame(const PublishEnvelope& envelope) {
  UdsResponseFrame frame;
  frame.request_id = envelope.request_id;
  frame.trace_id = envelope.trace_id;
  if (!envelope.session_id.empty()) {
    frame.session_id = envelope.session_id;
  }
  frame.disposition = map_disposition(envelope);

  if (frame.disposition == UdsResponseDisposition::AcceptedAsync &&
      !envelope.result_id.empty()) {
    frame.receipt_ref = envelope.result_id;
  }

  if (frame.disposition == UdsResponseDisposition::Rejected && !envelope.payload.empty()) {
    frame.error_ref = envelope.payload;
  }

  if (envelope.agent_result.has_value()) {
    frame.agent_result = envelope.agent_result;
  }

  return frame;
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

  const std::string_view payload_view(
      reinterpret_cast<const char*>(active_payload_.data()),
      active_payload_.size());

  const auto decoded = decode_request_frame(payload_view, active_payload_.size());
  if (!decoded.ok()) {
    return packet;
  }

  packet.entry_type = "daemon";
  packet.protocol_kind = "ipc_uds";

  packet.packet_id = decoded.frame.command_kind() == DaemonCommandKind::Ping
                         ? std::string("ping")
                         : (!decoded.frame.request_id.empty() ? decoded.frame.request_id
                                                              : decoded.frame.command);
  packet.payload = decoded.frame.payload;
  packet.async_preferred =
      decoded.frame.async_preference == DaemonAsyncPreference::PreferAsync;

  const auto peer_ref = decoded.frame.args.find("peer_ref");
  if (peer_ref != decoded.frame.args.end()) {
    packet.peer_ref = peer_ref->second;
  }

  return packet;
}

bool DaemonProtocolAdapter::encode(const PublishEnvelope& envelope) {
  if (!ipc_ || !active_channel_.has_consistent_values()) {
    return false;
  }

  const std::string response_json = encode_response_frame(build_response_frame(envelope));

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
