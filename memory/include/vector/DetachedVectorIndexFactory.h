#pragma once

#include <filesystem>
#include <memory>

#include "config/MemoryConfig.h"
#include "vector/VectorMemoryIndexAdapter.h"

namespace dasall::memory {

[[nodiscard]] std::unique_ptr<VectorMemoryIndexAdapter>
create_detached_vector_index_adapter(const MemoryConfig& config,
                                     const std::filesystem::path& database_path);

}  // namespace dasall::memory