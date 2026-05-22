#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "AccessTypes.h"
#include "IAccessGateway.h"

namespace dasall::profiles {
class RuntimePolicySnapshot;
}

namespace dasall::infra::secret {
class ISecretManager;
}

namespace dasall::infra::policy {
class ISecurityPolicyManager;
}

namespace dasall::infra::diagnostics {
class IDiagnosticsService;
}

namespace dasall::knowledge {
class IKnowledgeService;
}

namespace dasall::access {

class AsyncTaskRegistry;

using AccessObservabilityEmitBackend =
    std::function<bool(std::string_view event_name,
                       const std::map<std::string, std::string>& fields)>;
using AsyncReceiptObserver =
  std::function<void(const AsyncTaskReceipt& receipt)>;

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
  bool derive_views_from_runtime_policy = false;
  std::shared_ptr<const dasall::profiles::RuntimePolicySnapshot> runtime_policy_snapshot;

  bool policy_backend_available = true;
  bool allow_submit = true;
  bool daemon_listener_ready = true;
  bool daemon_gateway_ready = true;
  bool daemon_bridge_reachable = true;
  bool daemon_diagnostics_enabled = false;
  std::string daemon_version = "v1";
  std::string daemon_profile_id = "daemon.default";
  std::string daemon_runtime_readiness_label = "default-ready";
  std::vector<std::string> daemon_runtime_degraded_reasons;
  std::shared_ptr<std::atomic_bool> daemon_diagnostics_enabled_state;
  std::shared_ptr<dasall::infra::diagnostics::IDiagnosticsService> diagnostics_service;
    std::shared_ptr<dasall::knowledge::IKnowledgeService> knowledge_service;
  std::shared_ptr<AsyncTaskRegistry> async_task_registry;
  std::shared_ptr<dasall::infra::secret::ISecretManager> ownership_secret_manager;
  std::shared_ptr<dasall::infra::policy::ISecurityPolicyManager> security_policy_manager;
  PublishBackend publish_backend{};
  AccessObservabilityEmitBackend observability_emit_backend{};
  AsyncReceiptObserver async_receipt_observer{};

  RuntimeDispatchBackend runtime_dispatch_backend{};
  RuntimeCancelBackend runtime_cancel_backend{};
};

struct GatewayAccessPipelineOptions {
  using RuntimeDispatchBackend =
      std::function<RuntimeDispatchResult(const RuntimeDispatchRequest& request)>;
  using PublishBackend =
      std::function<bool(const PublishEnvelope& envelope)>;

  AccessBootstrapConfig bootstrap_config{};
  AccessAuthView auth_view{};
  AccessAdmissionView admission_view{};
  AccessPublishView publish_view{};
  bool derive_views_from_runtime_policy = false;
  std::shared_ptr<const dasall::profiles::RuntimePolicySnapshot> runtime_policy_snapshot;
  std::shared_ptr<AsyncTaskRegistry> async_task_registry;
  std::shared_ptr<dasall::infra::secret::ISecretManager> ownership_secret_manager;
  std::shared_ptr<dasall::infra::policy::ISecurityPolicyManager> security_policy_manager;

  bool policy_backend_available = true;
  bool allow_submit = true;
  PublishBackend publish_backend{};
    AccessObservabilityEmitBackend observability_emit_backend{};

  RuntimeDispatchBackend runtime_dispatch_backend{};
};

// 受控组合根接缝：apps 只能通过 public factory 获取默认 gateway，
// 避免直接 include access/src 下的 concrete 头文件。
[[nodiscard]] std::shared_ptr<IAccessGateway> create_access_gateway(
    AccessGatewayFactoryOptions options = {});

[[nodiscard]] std::shared_ptr<IAccessGateway> create_daemon_access_gateway(
    DaemonAccessPipelineOptions options = {});

[[nodiscard]] std::shared_ptr<IAccessGateway> create_gateway_access_gateway(
    GatewayAccessPipelineOptions options = {});

}  // namespace dasall::access