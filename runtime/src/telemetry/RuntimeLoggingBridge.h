#pragma once

#include <memory>

#include "logging/ILogger.h"

#include "RuntimeEventBus.h"

namespace dasall::runtime {

class RuntimeLoggingBridge final {
 public:
  explicit RuntimeLoggingBridge(std::shared_ptr<infra::logging::ILogger> logger);

  [[nodiscard]] infra::logging::LogWriteResult handle(
      const RuntimeEventEnvelope& event) const;

 private:
  std::shared_ptr<infra::logging::ILogger> logger_;
};

}  // namespace dasall::runtime