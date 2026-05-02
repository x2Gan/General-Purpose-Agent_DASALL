#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <string_view>

#include "AccessTypes.h"
#include "IAccessGateway.h"

namespace dasall::infra::diagnostics {
class IDiagnosticsService;
}

namespace dasall::access {

class AsyncTaskRegistry;

struct AccessGatewayFactoryOptions {
  using SubmitPipeline =
      std::function<RuntimeDispatchResult(const InboundPacket& packet)>;
  using PublishBackend =
      std::function<bool(const PublishEnvelope& envelope)>;
    using ShutdownObserver = std::function<void(std::size_t abandoned_requests)>;

  SubmitPipeline submit_pipeline{};
  PublishBackend publish_backend{};
    ShutdownObserver shutdown_observer{};
};

struct DaemonAccessPipelineOptions {
    using RuntimeDispatchBackend =
            std::function<RuntimeDispatchResult(const RuntimeDispatchRequest& request)>;
    using RuntimeCancelBackend =
            std::function<bool(std::string_view request_id, std::string_view actor_ref)>;
    using PublishBackend =
        std::function<bool(const PublishEnvelope& envelope)>;

    AccessBootstrapConfig bootstrap_config{};
    AccessAuthView auth_view{};
    AccessAdmissionView admission_view{};
    AccessPublishView publish_view{};

    bool policy_backend_available = true;
    bool allow_submit = true;
    bool daemon_listener_ready = true;
    bool daemon_gateway_ready = true;
    bool daemon_bridge_reachable = true;
    bool daemon_diagnostics_enabled = false;
    std::string daemon_version = "v1";
    std::string daemon_profile_id = "daemon.default";
    std::shared_ptr<std::atomic_bool> daemon_diagnostics_enabled_state;
    std::shared_ptr<dasall::infra::diagnostics::IDiagnosticsService> diagnostics_service;
    std::shared_ptr<AsyncTaskRegistry> async_task_registry;
    PublishBackend publish_backend{};

    RuntimeDispatchBackend runtime_dispatch_backend{};
    RuntimeCancelBackend runtime_cancel_backend{};
};

// 受控组合根接缝：apps 只能通过 public factory 获取默认 gateway，
// 避免直接 include access/src 下的 concrete 头文件。
[[nodiscard]] std::shared_ptr<IAccessGateway> create_access_gateway(
    AccessGatewayFactoryOptions options = {});

[[nodiscard]] std::shared_ptr<IAccessGateway> create_daemon_access_gateway(
    DaemonAccessPipelineOptions options = {});

}  // namespace dasall::access