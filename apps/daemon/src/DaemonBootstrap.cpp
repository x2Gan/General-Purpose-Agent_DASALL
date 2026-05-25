#include "DaemonBootstrap.h"

#include <iostream>
#include <string>
#include <utility>

#include "../../../access/src/daemon/TuiIpcProtocolAdapter.h"

namespace dasall::apps::daemon {

namespace {

dasall::access::daemon::TuiIpcSessionStore g_tui_ipc_session_store;

[[nodiscard]] std::string resolve_daemon_peer_ref(
    const dasall::access::LocalPeerUidFact& peer_fact,
    std::string_view decoded_peer_ref = {}) {
  if (peer_fact.eligible_for_local_trusted) {
    return "local_trusted:" + std::to_string(peer_fact.peer_uid);
  }
  if (!decoded_peer_ref.empty()) {
    return std::string(decoded_peer_ref);
  }
  return "untrusted";
}

}  // namespace

DaemonBootstrap::~DaemonBootstrap() {
  request_dispatch_stop(false);
  stop_dispatch_workers();
}

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
    std::cerr << "[dasall-daemon] bootstrap run failed: inconsistent process context\n";
    return false;
  }

  stop_requested_.store(false);
  configure_from_context(context);
  {
    std::lock_guard<std::mutex> lock(dispatch_mutex_);
    pending_channels_.clear();
    dispatch_stop_requested_ = false;
    dispatch_failed_ = false;
  }
  processing_connections_.store(0U);

  if (!ipc_ || !gateway_ || !listener_host_.has_value()) {
    std::cerr << "[dasall-daemon] bootstrap run failed: missing ipc/gateway/listener host\n";
    return false;
  }

  if (!lifecycle_.start()) {
    std::cerr << "[dasall-daemon] bootstrap run failed: lifecycle start rejected\n";
    return false;
  }

  if (!gateway_->is_ready()) {
    std::cerr << "[dasall-daemon] bootstrap run failed: access gateway not ready\n";
    (void)lifecycle_.mark_failed();
    return false;
  }

  if (!lifecycle_.mark_binding()) {
    std::cerr << "[dasall-daemon] bootstrap run failed: lifecycle binding transition rejected\n";
    (void)lifecycle_.mark_failed();
    return false;
  }

  start_dispatch_workers(context.bootstrap_config.dispatch_workers);

  dasall::platform::IpcEndpoint endpoint;
  endpoint.socket_path = context.bootstrap_config.socket_path;

  const auto bind_result = listener_host_->bind(endpoint);
  if (!bind_result.ok()) {
    if (bind_result.error.has_value() && !bind_result.error->detail.empty()) {
      std::cerr << "[dasall-daemon] listener bind failed: "
                << bind_result.error->detail << "\n";
    } else {
      std::cerr << "[dasall-daemon] listener bind failed\n";
    }
    request_dispatch_stop(true);
    stop_dispatch_workers();
    (void)lifecycle_.mark_failed();
    return false;
  }

  listener_host_->set_connection_handler(
      [this](const dasall::platform::IpcChannelHandle& channel) {
        return !enqueue_connection(channel);
      });

  if (!lifecycle_.mark_ready()) {
    std::cerr << "[dasall-daemon] bootstrap run failed: lifecycle ready transition rejected\n";
    request_dispatch_stop(true);
    stop_dispatch_workers();
    (void)listener_host_->close();
    (void)lifecycle_.mark_failed();
    return false;
  }

  if (supervisor_adapter_.has_value()) {
    (void)supervisor_adapter_->notify_ready();
  }

  const auto loop_result =
      listener_host_->accept_loop(stop_requested_, kAcceptDeadlineMs);
  stop_dispatch_workers();
  if (supervisor_adapter_.has_value()) {
    (void)supervisor_adapter_->notify_stopping();
  }
  (void)listener_host_->close();
  if (!loop_result.ok()) {
    if (loop_result.error.has_value() && !loop_result.error->detail.empty()) {
      std::cerr << "[dasall-daemon] listener accept loop failed: "
                << loop_result.error->detail << "\n";
    } else {
      std::cerr << "[dasall-daemon] listener accept loop failed\n";
    }
    (void)lifecycle_.mark_failed();
    return false;
  }

  if (!loop_result.value.value_or(false)) {
    std::cerr << "[dasall-daemon] listener accept loop stopped before successful shutdown\n";
    (void)lifecycle_.mark_failed();
    return false;
  }

  if (dispatch_failed_) {
    std::cerr << "[dasall-daemon] request dispatch worker failed\n";
    (void)lifecycle_.mark_failed();
    return false;
  }

  return true;
}

void DaemonBootstrap::stop(std::chrono::milliseconds drain_timeout) {
  stop_requested_.store(true);
  if (listener_host_.has_value()) {
    (void)listener_host_->close();
  }
  if (gateway_) {
    gateway_->shutdown(drain_timeout);
  }
  (void)lifecycle_.shutdown(drain_timeout);
}

std::size_t DaemonBootstrap::active_connection_count() const {
  std::lock_guard<std::mutex> lock(dispatch_mutex_);
  return pending_channels_.size() + processing_connections_.load();
}

void DaemonBootstrap::configure_from_context(const DaemonProcessContext& context) {
  ipc_ = context.ipc;
  gateway_ = context.access_gateway;
  effective_profile_id_ = context.effective_profile_id;
  listener_host_.emplace(ipc_);
  listener_host_->set_listen_options(dasall::platform::ListenOptions{
      .backlog = context.bootstrap_config.listen_backlog,
      .max_payload_bytes = context.bootstrap_config.max_payload_bytes,
  });
  {
    std::lock_guard<std::mutex> lock(g_tui_ipc_session_store.mutex);
    g_tui_ipc_session_store.next_session_id = 0U;
    g_tui_ipc_session_store.sessions.clear();
  }
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

DaemonBootstrap::ConnectionHandlingDisposition DaemonBootstrap::handle_connection(
    const dasall::platform::IpcChannelHandle& channel) {
  if (!channel.has_consistent_values()) {
    return ConnectionHandlingDisposition::FatalError;
  }

  if (!lifecycle_.begin_request()) {
    return ConnectionHandlingDisposition::Dropped;
  }

  struct RequestScope final {
    DaemonLifecycleController* lifecycle = nullptr;
    ~RequestScope() {
      if (lifecycle != nullptr) {
        lifecycle->finish_request();
      }
    }
  } request_scope{.lifecycle = &lifecycle_};

  // 接收请求 payload
  const auto recv_result = ipc_->receive(channel, receive_deadline_ms_);
  if (!recv_result.ok()) {
    return ConnectionHandlingDisposition::FatalError;
  }
  if (!recv_result.value.has_value() || recv_result.value->peer_closed) {
    return ConnectionHandlingDisposition::Dropped;
  }

  const auto& raw_payload = recv_result.value->data;
  if (raw_payload.empty()) {
    return ConnectionHandlingDisposition::Dropped;
  }

  dasall::access::daemon::DaemonProtocolAdapter peer_adapter(ipc_);
  const auto peer_fact =
      peer_adapter.describe_local_peer_uid_fact(channel, "actor://daemon/local");

  dasall::access::daemon::TuiIpcProtocolAdapter tui_adapter(ipc_);
  tui_adapter.set_active_channel(channel, raw_payload);
  if (tui_adapter.payload_looks_like_tui_ipc()) {
    const auto decoded = tui_adapter.decode_tui_ipc_request();
    const auto response = tui_adapter.dispatch_tui_ipc_operation(
        decoded,
        *gateway_,
        g_tui_ipc_session_store,
        resolve_daemon_peer_ref(peer_fact),
        effective_profile_id_);
    return tui_adapter.encode_tui_ipc_response(response)
               ? ConnectionHandlingDisposition::Completed
               : ConnectionHandlingDisposition::FatalError;
  }

  // 构造 DaemonProtocolAdapter 并注入连接上下文
  dasall::access::daemon::DaemonProtocolAdapter adapter(ipc_);
  adapter.set_active_channel(channel, raw_payload);

  // 解码请求 -> InboundPacket
  auto packet = adapter.decode();
  if (packet.entry_type.empty()) {
    return ConnectionHandlingDisposition::FatalError;
  }

  // 将 peer identity 注入 packet.peer_ref（以 "local_trusted:" 前缀标记）
  packet.peer_ref = resolve_daemon_peer_ref(peer_fact, packet.peer_ref);

  // 通过 IAccessGateway 主链处理请求
  const auto dispatch_result = gateway_->submit(packet);

  // 若有 PublishEnvelope 则直接编码发回
  if (dispatch_result.publish_envelope.has_value()) {
    return adapter.encode(*dispatch_result.publish_envelope)
               ? ConnectionHandlingDisposition::Completed
               : ConnectionHandlingDisposition::FatalError;
  }

  // 异步接受：生成最小 accepted 响应
  if (dispatch_result.disposition == dasall::access::AccessDisposition::AcceptedAsync) {
    dasall::access::PublishEnvelope async_envelope;
    async_envelope.protocol_status_hint = "202";
    async_envelope.result_id =
        dispatch_result.receipt_ref.value_or("async-receipt");
    return adapter.encode(async_envelope)
           ? ConnectionHandlingDisposition::Completed
           : ConnectionHandlingDisposition::FatalError;
  }

  // 拒绝路径：生成 rejected 响应
  dasall::access::PublishEnvelope reject_envelope;
  reject_envelope.protocol_status_hint = "400";
  reject_envelope.payload =
      dispatch_result.error_ref.value_or("rejected");
  return adapter.encode(reject_envelope)
             ? ConnectionHandlingDisposition::Completed
             : ConnectionHandlingDisposition::FatalError;
}

bool DaemonBootstrap::enqueue_connection(
    const dasall::platform::IpcChannelHandle& channel) {
  std::lock_guard<std::mutex> lock(dispatch_mutex_);
  if (dispatch_stop_requested_ || dispatch_failed_) {
    return false;
  }

  pending_channels_.push_back(channel);
  dispatch_cv_.notify_one();
  return true;
}

void DaemonBootstrap::start_dispatch_workers(const std::uint32_t worker_count) {
  dispatch_workers_.clear();
  dispatch_workers_.reserve(worker_count);
  for (std::uint32_t index = 0; index < worker_count; ++index) {
    dispatch_workers_.emplace_back([this]() {
      dispatch_worker_loop();
    });
  }
}

void DaemonBootstrap::stop_dispatch_workers() {
  {
    std::lock_guard<std::mutex> lock(dispatch_mutex_);
    dispatch_stop_requested_ = true;
  }
  dispatch_cv_.notify_all();

  for (auto& worker : dispatch_workers_) {
    if (worker.joinable()) {
      worker.join();
    }
  }
  dispatch_workers_.clear();

  std::deque<dasall::platform::IpcChannelHandle> abandoned_channels;
  {
    std::lock_guard<std::mutex> lock(dispatch_mutex_);
    abandoned_channels.swap(pending_channels_);
  }

  for (const auto& channel : abandoned_channels) {
    if (ipc_ != nullptr) {
      (void)ipc_->close(channel);
    }
  }
}

void DaemonBootstrap::dispatch_worker_loop() {
  while (true) {
    std::optional<dasall::platform::IpcChannelHandle> channel;
    {
      std::unique_lock<std::mutex> lock(dispatch_mutex_);
      dispatch_cv_.wait(lock, [this]() {
        return dispatch_stop_requested_ || !pending_channels_.empty();
      });

      if (pending_channels_.empty()) {
        if (dispatch_stop_requested_) {
          return;
        }
        continue;
      }

      channel = pending_channels_.front();
      pending_channels_.pop_front();
      ++processing_connections_;
    }

    const auto disposition = handle_connection(*channel);
    const auto close_result = ipc_ != nullptr
                                  ? ipc_->close(*channel)
                                  : dasall::platform::PlatformResult<bool>::success(true);
    --processing_connections_;

    if (disposition == ConnectionHandlingDisposition::FatalError || !close_result.ok()) {
      request_dispatch_stop(true);
      return;
    }
  }
}

void DaemonBootstrap::request_dispatch_stop(const bool fatal_error) {
  stop_requested_.store(true);
  if (listener_host_.has_value()) {
    (void)listener_host_->close();
  }

  {
    std::lock_guard<std::mutex> lock(dispatch_mutex_);
    dispatch_failed_ = dispatch_failed_ || fatal_error;
    dispatch_stop_requested_ = true;
  }
  dispatch_cv_.notify_all();
}
}  // namespace dasall::apps::daemon
