#include <exception>
#include <iostream>

#include "data/DataProjectionCache.h"
#include "support/TestAssertions.h"

namespace {

using dasall::services::DataQueryRequest;
using dasall::services::ServiceCallContext;
using dasall::services::ServiceDataFreshness;
using dasall::services::internal::DataProjectionCache;
using dasall::services::internal::DataProjectionCacheDependencies;
using dasall::services::internal::ProjectionCacheState;

[[nodiscard]] ServiceCallContext make_context() {
  dasall::contracts::RuntimeBudget budget;
  budget.max_latency_ms = 3000;

  return ServiceCallContext{
      .request_id = "req-020",
      .session_id = "session-020",
      .trace_id = "trace-020",
      .tool_call_id = "tool-020",
      .goal_id = "goal-020",
      .budget_guard = budget,
      .deadline_ms = 9000,
  };
}

[[nodiscard]] DataQueryRequest make_request(std::string dataset = "devices",
                                            std::string filters_json = "{\"region\":\"lab\"}",
                                            std::string projection = "status") {
  return DataQueryRequest{
      .context = make_context(),
      .dataset = std::move(dataset),
      .filters_json = std::move(filters_json),
      .projection = std::move(projection),
      .freshness = ServiceDataFreshness::strict,
  };
}

void test_data_projection_cache_reports_miss_before_store() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  std::uint64_t now_ms = 1000U;
  const DataProjectionCache cache(DataProjectionCacheDependencies{
      .ttl_ms = 500U,
      .now_ms = [&]() { return now_ms; },
  });

  const auto result = cache.lookup(make_request(), ServiceDataFreshness::strict);

  assert_equal(static_cast<int>(ProjectionCacheState::miss),
               static_cast<int>(result.state),
               "lookup before store should report miss");
  assert_true(!result.from_cache, "cache miss should not mark from_cache");
  assert_true(!result.snapshot.has_value(), "cache miss should not return snapshot");
}

void test_data_projection_cache_returns_fresh_hit_with_from_cache_flag() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  std::uint64_t now_ms = 1000U;
  DataProjectionCache cache(DataProjectionCacheDependencies{
      .ttl_ms = 500U,
      .now_ms = [&]() { return now_ms; },
  });
  const auto request = make_request();

  cache.store(request, "[{\"id\":1,\"state\":\"ready\"}]", "cache://devices/status");
  now_ms = 1200U;

  const auto result = cache.lookup(request, ServiceDataFreshness::strict);

  assert_equal(1, static_cast<int>(cache.entry_count()), "store should create one cache entry");
  assert_equal(static_cast<int>(ProjectionCacheState::hit_fresh),
               static_cast<int>(result.state),
               "fresh lookup should report hit_fresh");
  assert_true(result.from_cache, "fresh hit should mark from_cache=true");
  assert_true(result.snapshot.has_value(), "fresh hit should return cached snapshot");
  assert_equal(DataProjectionCache::make_cache_key(request),
               result.snapshot->cache_key,
               "cache key should be derived from dataset/filters/projection");
}

void test_data_projection_cache_reports_stale_snapshot_for_strict_reads() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  std::uint64_t now_ms = 1000U;
  DataProjectionCache cache(DataProjectionCacheDependencies{
      .ttl_ms = 100U,
      .now_ms = [&]() { return now_ms; },
  });
  const auto request = make_request();

  cache.store(request, "[{\"id\":1}]", "cache://devices/stale");
  now_ms = 1205U;

  const auto result = cache.lookup(request, ServiceDataFreshness::strict);

  assert_equal(static_cast<int>(ProjectionCacheState::hit_stale),
               static_cast<int>(result.state),
               "expired snapshot should report hit_stale");
  assert_true(!result.from_cache,
              "strict freshness should not allow returning stale cache as from_cache");
  assert_true(result.snapshot.has_value(), "strict stale lookup should still expose snapshot facts");
  assert_equal(205,
               static_cast<int>(result.snapshot->age_ms),
               "stale lookup should expose cache age in milliseconds");
}

void test_data_projection_cache_allows_stale_reads_when_requested() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  std::uint64_t now_ms = 1000U;
  DataProjectionCache cache(DataProjectionCacheDependencies{
      .ttl_ms = 100U,
      .now_ms = [&]() { return now_ms; },
  });
  const auto request = make_request("devices", "{\"region\":\"lab\"}", "summary");

  cache.store(request, "[{\"id\":1,\"cached\":true}]", "cache://devices/summary");
  now_ms = 1300U;

  const auto result = cache.lookup(request, ServiceDataFreshness::allow_stale);

  assert_equal(static_cast<int>(ProjectionCacheState::hit_stale),
               static_cast<int>(result.state),
               "allow_stale should still report stale state");
  assert_true(result.from_cache,
              "allow_stale should return stale snapshot with from_cache=true");
  assert_true(result.snapshot.has_value(), "allow_stale should return cached snapshot");
  assert_equal(std::string("[{\"id\":1,\"cached\":true}]"),
               result.snapshot->rows_json,
               "allow_stale should preserve cached rows payload");
}

}  // namespace

int main() {
  try {
    test_data_projection_cache_reports_miss_before_store();
    test_data_projection_cache_returns_fresh_hit_with_from_cache_flag();
    test_data_projection_cache_reports_stale_snapshot_for_strict_reads();
    test_data_projection_cache_allows_stale_reads_when_requested();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}