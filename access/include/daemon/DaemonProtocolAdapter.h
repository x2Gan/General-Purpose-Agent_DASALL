#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "AccessTypes.h"
#include "IIPC.h"
#include "IProtocolAdapter.h"

namespace dasall::access::daemon {

/// DaemonProtocolAdapter — 本地控制面 IPC 协议适配器
///
/// 职责：
///   1. 实现 IProtocolAdapter（can_handle / decode / encode）
///   2. 维护单次连接上下文（channel handle + raw payload）
///   3. 提供 describe_local_peer_uid_fact() 以驱动 local trusted 判定
///
/// 边界约束：
///   - 仅处理 entry_type="daemon" + protocol_kind="ipc_uds"
///   - 每次连接前必须调用 set_active_channel()，decode/encode 依赖此上下文
///   - 编解码失败返回空对象或 false，不抛出异常
///   - 不保留跨连接状态；peer identity 由调用方注入 InboundPacket.peer_ref
class DaemonProtocolAdapter : public dasall::access::IProtocolAdapter {
 public:
  explicit DaemonProtocolAdapter(std::shared_ptr<dasall::platform::IIPC> ipc);

  // -------------------------------------------------------------------------
  // IProtocolAdapter 接口实现
  // -------------------------------------------------------------------------

  /// 判断此 adapter 是否可以处理指定的 entry_type 和 protocol_kind。
  /// 仅接受 ("daemon", "ipc_uds") 组合。
  [[nodiscard]] bool can_handle(std::string_view entry_type,
                                std::string_view protocol_kind) const override;

  /// 将活动连接的原始 payload 解析为 InboundPacket。
  /// 调用前须先调用 set_active_channel()；若 payload 为空则返回空 packet。
  [[nodiscard]] InboundPacket decode() override;

  /// 将 PublishEnvelope 序列化为 JSON 并发送到活动连接通道。
  /// 调用前须先调用 set_active_channel()；发送失败返回 false。
  [[nodiscard]] bool encode(const PublishEnvelope& envelope) override;

  // -------------------------------------------------------------------------
  // 连接上下文注入
  // -------------------------------------------------------------------------

  /// 在处理每条新连接前注入 channel handle 和已接收的原始 payload。
  void set_active_channel(dasall::platform::IpcChannelHandle channel,
                          std::vector<std::uint8_t> payload);

  // -------------------------------------------------------------------------
  // Peer identity 接缝（复用 ACC-TODO-037 成果）
  // -------------------------------------------------------------------------

  /// 从 IPC channel 读取 peer identity 事实，用于驱动 local trusted 判定。
  /// 若 IPC 未初始化或 describe_peer 失败，返回全 false 的默认 fact。
  [[nodiscard]] LocalPeerUidFact describe_local_peer_uid_fact(
      const dasall::platform::IpcChannelHandle& handle,
      std::string actor_ref) const;

 private:
  std::shared_ptr<dasall::platform::IIPC> ipc_;
  dasall::platform::IpcChannelHandle active_channel_;
  std::vector<std::uint8_t> active_payload_;
};

}  // namespace dasall::access::daemon
