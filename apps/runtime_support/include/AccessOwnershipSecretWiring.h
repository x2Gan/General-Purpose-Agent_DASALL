#pragma once

#include <memory>

#include "RuntimeDependencySet.h"

namespace dasall::apps::runtime_support {

// App composition owners project the runtime-composed secret seam into Access
// pipeline options so accepted_async ownership HMAC can reuse infra's standard
// secret manager path without introducing a second secret channel.
template <typename PipelineOptions>
inline void wire_runtime_secret_manager_into_access_ownership_seam(
    const std::shared_ptr<runtime::RuntimeDependencySet>& dependency_set,
    PipelineOptions& options) {
  options.ownership_secret_manager =
      dependency_set != nullptr ? dependency_set->secret_manager : nullptr;
}

}  // namespace dasall::apps::runtime_support