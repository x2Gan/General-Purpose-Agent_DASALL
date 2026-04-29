#include "DaemonBootstrap.h"

#include <string>
#include <utility>

namespace dasall::apps::daemon {

DaemonBootstrap::DaemonBootstrap(
    std::shared_ptr<dasall::platform::IIPC> ipc,
    std::shared_ptr<dasall::access::IAccessGateway> gateway)
    : ipc_(std::move(ipc)),
      gateway_(std::move(gateway)) {
  if (ipc_) {
    listener_host_.emplace(ipc_);
  }
}

std::optional<DaemonProcessContext> DaemonBootstrap::build(
    const DaemonBootstrapConfig& config,
    BuildDependencies dependencies) {
  if (!config.has_consistent_values() || !dependencies.has_consistent_values()) {
    return std::nullopt;
  }

  if (!dependencies.access_gateway->is_ready()) {
    return std::nullopt;
  }

  return DaemonProcessContext{
      .bootstrap_config = config,
      .effective_profile_id = std::move(dependencies.effective_profile_id),
      .ipc = std::move(dependencies.ipc),
      .access_gateway = std::move(dependencies.access_gateway),
      .watchdog_service = std::move(dependencies.watchdog_service),
      .config_revision = std::move(dependencies.config_revision),
  };
}

bool DaemonBootstrap::run(const DaemonProcessContext& context) {
  if (!context.has_consistent_values()) {
    return false;
  }

  stop_requested_.store(false);
  configure_from_context(context);

  if (!ipc_ || !gateway_ || !listener_host_.has_value()) {
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

  dasall::platform::IpcEndpoint endpoint;
  endpoint.socket_path = context.bootstrap_config.socket_path;

  const auto bind_result = listener_host_->bind(endpoint);
  if (!bind_result.ok()) {
    (void)lifecycle_.mark_failed();
    return false;
  }

  listener_host_->set_connection_handler(
      [this](const dasall::platform::IpcChannelHandle& channel) {
        return handle_connection(channel);
      });

  if (!lifecycle_.mark_ready()) {
    (void)listener_host_->close();
    (void)lifecycle_.mark_failed();
    return false;
  }

  if (supervisor_adapter_.has_value()) {
    (void)supervisor_adapter_->notify_ready();
  }

  const auto loop_result =
      listener_host_->accept_loop(stop_requested_, kAcceptDeadlineMs);
  if (supervisor_adapter_.has_value()) {
    (void)supervisor_adapter_->notify_stopping();
  }
  (void)listener_host_->close();
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

void DaemonBootstrap::configure_from_context(const DaemonProcessContext& context) {
  ipc_ = context.ipc;
  gateway_ = context.access_gateway;
  listener_host_.emplace(ipc_);
  listener_host_->set_listen_options(dasall::platform::ListenOptions{
      .backlog = context.bootstrap_config.listen_backlog,
      .max_payload_bytes = context.bootstrap_config.max_payload_bytes,
  });
  supervisor_adapter_.emplace(
      context.watchdog_service,
      DaemonSupervisorAdapterOptions{
          .watchdog_enabled = context.bootstrap_config.watchdog_enabled,
          .watchdog_entity_id = "daemon.main_loop",
          .watchdog_timeout_ms = 15000U,
          .watchdog_grace_ms = 2000U,
      });
  receive_deadline_ms_ = context.bootstrap_config.dispatch_timeout_ms;
}

bool DaemonBootstrap::handle_connection(
    const dasall::platform::IpcChannelHandle& channel) {
  if (!channel.has_consistent_values()) {
    return false;
  }

  // 接收请求 payload
  const auto recv_result = ipc_->receive(channel, receive_deadline_ms_);
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
}  // namespace dasall::apps::daemon
