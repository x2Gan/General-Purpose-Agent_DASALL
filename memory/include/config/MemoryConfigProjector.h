#pragma once

#include <optional>

#include "BuildProfileManifest.h"
#include "RuntimePolicySnapshot.h"
#include "config/MemoryConfig.h"

namespace dasall::memory::config {

[[nodiscard]] std::optional<MemoryConfig> project_memory_config(
    const profiles::RuntimePolicySnapshot& snapshot,
    const profiles::BuildProfileManifest& manifest);

}  // namespace dasall::memory::config