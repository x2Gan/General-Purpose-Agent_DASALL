#pragma once

#include <functional>
#include <memory>

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

// 受控组合根接缝：apps 只能通过 public factory 获取默认 gateway，
// 避免直接 include access/src 下的 concrete 头文件。
[[nodiscard]] std::shared_ptr<IAccessGateway> create_access_gateway(
    AccessGatewayFactoryOptions options = {});

}  // namespace dasall::access