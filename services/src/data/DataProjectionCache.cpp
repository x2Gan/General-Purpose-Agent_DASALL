#include "data/DataProjectionCache.h"

#include <chrono>
#include <utility>

namespace dasall::services::internal {

namespace {

[[nodiscard]] std::string default_cache_ref(const std::string& cache_key) {
  return "cache://data/" + cache_key;
}

}  // namespace

std::string_view projection_cache_state_name(ProjectionCacheState state) {
  switch (state) {
    case ProjectionCacheState::miss:
      return "miss";
    case ProjectionCacheState::hit_fresh:
      return "hit_fresh";
    case ProjectionCacheState::hit_stale:
      return "hit_stale";
  }

  return "unknown";
}

DataProjectionCache::DataProjectionCache(DataProjectionCacheDependencies dependencies)
    : dependencies_(std::move(dependencies)) {}

std::string DataProjectionCache::make_cache_key(const DataQueryRequest& request) {
  return request.dataset + "|" + request.filters_json + "|" + request.projection;
}

void DataProjectionCache::store(const DataQueryRequest& request,
                                std::string rows_json,
                                std::string cache_ref) {
  const auto cache_key = make_cache_key(request);
  if (cache_ref.empty()) {
    cache_ref = default_cache_ref(cache_key);
  }

  std::lock_guard<std::mutex> lock(mutex_);
  cache_[cache_key] = ProjectionCacheRecord{
      .rows_json = std::move(rows_json),
      .cache_ref = std::move(cache_ref),
      .cached_at_ms = current_time_ms(),
  };
}

ProjectionCacheLookup DataProjectionCache::lookup(const DataQueryRequest& request,
                                                  ServiceDataFreshness freshness) const {
  const auto cache_key = make_cache_key(request);

  std::lock_guard<std::mutex> lock(mutex_);
  const auto cache_it = cache_.find(cache_key);
  if (cache_it == cache_.end()) {
    return ProjectionCacheLookup{
        .state = ProjectionCacheState::miss,
        .from_cache = false,
        .snapshot = std::nullopt,
    };
  }

  const auto now_ms = current_time_ms();
  const auto age_ms = now_ms >= cache_it->second.cached_at_ms ? now_ms - cache_it->second.cached_at_ms
                                                               : 0U;
  const auto stale = age_ms > dependencies_.ttl_ms;

  return ProjectionCacheLookup{
      .state = stale ? ProjectionCacheState::hit_stale : ProjectionCacheState::hit_fresh,
      .from_cache = !stale || freshness == ServiceDataFreshness::allow_stale,
      .snapshot = CachedProjectionSnapshot{
          .cache_key = cache_key,
          .rows_json = cache_it->second.rows_json,
          .cache_ref = cache_it->second.cache_ref,
          .cached_at_ms = cache_it->second.cached_at_ms,
          .age_ms = age_ms,
      },
  };
}

std::size_t DataProjectionCache::entry_count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return cache_.size();
}

std::uint64_t DataProjectionCache::current_time_ms() const {
  if (dependencies_.now_ms) {
    return dependencies_.now_ms();
  }

  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

}  // namespace dasall::services::internal