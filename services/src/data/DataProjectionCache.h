#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "ServiceTypes.h"

namespace dasall::services::internal {

enum class ProjectionCacheState {
  miss,
  hit_fresh,
  hit_stale,
};

struct CachedProjectionSnapshot {
  std::string cache_key;
  std::string rows_json;
  std::string cache_ref;
  std::uint64_t cached_at_ms = 0U;
  std::uint64_t age_ms = 0U;
};

struct ProjectionCacheLookup {
  ProjectionCacheState state = ProjectionCacheState::miss;
  bool from_cache = false;
  std::optional<CachedProjectionSnapshot> snapshot;
};

struct DataProjectionCacheDependencies {
  std::uint64_t ttl_ms = 0U;
  std::function<std::uint64_t()> now_ms;
};

class DataProjectionCache {
 public:
  explicit DataProjectionCache(DataProjectionCacheDependencies dependencies);

  [[nodiscard]] static std::string make_cache_key(const DataQueryRequest& request);

  void store(const DataQueryRequest& request,
             std::string rows_json,
             std::string cache_ref = {});

  [[nodiscard]] ProjectionCacheLookup lookup(const DataQueryRequest& request,
                                             ServiceDataFreshness freshness) const;

  [[nodiscard]] std::size_t entry_count() const;

 private:
  struct ProjectionCacheRecord {
    std::string rows_json;
    std::string cache_ref;
    std::uint64_t cached_at_ms = 0U;
  };

  [[nodiscard]] std::uint64_t current_time_ms() const;

  DataProjectionCacheDependencies dependencies_;
  mutable std::mutex mutex_;
  std::unordered_map<std::string, ProjectionCacheRecord> cache_;
};

[[nodiscard]] std::string_view projection_cache_state_name(ProjectionCacheState state);

}  // namespace dasall::services::internal