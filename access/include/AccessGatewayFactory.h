#pragma once

#include <functional>
#include <memory>
#include <string_view>

#include "AccessTypes.h"
#include "IAccessGateway.h"

namespace dasall::access {

struct AccessGatewayFactoryOptions {
  using SubmitPipeline =
      std::function<RuntimeDispatchResult(const InboundPacket& packet)>;
  using PublishBackend =
      std::function<bool(const PublishEnvelope& envelope)>;

  SubmitPipeline submit_pipeline{};
  PublishBackend publish_backend{};
};

struct DaemonAccessPipelineOptions {
    using RuntimeDispatchBackend =
            std::function<RuntimeDispatchResult(const RuntimeDispatchRequest& request)>;
    using RuntimeCancelBackend =
            std::function<bool(std::string_view request_id, std::string_view actor_ref)>;

    AccessBootstrapConfig bootstrap_config{};
    AccessAuthView auth_view{};
    AccessAdmissionView admission_view{};
    AccessPublishView publish_view{};

    bool policy_backend_available = true;
    bool allow_submit = true;

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