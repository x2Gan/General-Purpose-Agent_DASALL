#pragma once

#include <filesystem>
#include <memory>
#include <string_view>
#include <vector>

#include "config/MemoryConfig.h"
#include "vector/VectorMemoryIndexAdapter.h"

namespace dasall::memory {

[[nodiscard]] std::unique_ptr<VectorMemoryIndexAdapter>
create_detached_vector_index_adapter(const MemoryConfig& config,
                                     const std::filesystem::path& database_path);

[[nodiscard]] bool detached_vector_index_backend_available(
    const MemoryConfig& config,
    const std::filesystem::path& database_path);

[[nodiscard]] bool detached_vector_local_query_encoder_available(
    const MemoryConfig& config);

[[nodiscard]] std::vector<float> encode_detached_vector_query_for_local_fallback(
    const MemoryConfig& config,
    std::string_view query_text);

[[nodiscard]] std::vector<VectorHit> search_detached_vector_index_by_embedding(
    const MemoryConfig& config,
    const std::filesystem::path& database_path,
    const std::vector<float>& query_embedding,
    int top_k);

}  // namespace dasall::memory