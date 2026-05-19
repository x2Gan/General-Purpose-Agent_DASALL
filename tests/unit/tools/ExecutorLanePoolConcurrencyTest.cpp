#include <algorithm>
#include <array>
#include <condition_variable>
#include <exception>
#include <iostream>
#include <mutex>
#include <thread>

#include "config/ToolConfigAdapter.h"
#include "execution/ExecutorLanePool.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] dasall::tools::config::ToolTimeoutView make_timeout_view(
    std::uint32_t max_tool_calls) {
  return dasall::tools::config::ToolTimeoutView{
      .tool = {.timeout_ms = 2500, .retry_budget = 1U, .circuit_breaker_threshold = 4U},
      .mcp = {.timeout_ms = 2000, .retry_budget = 1U, .circuit_breaker_threshold = 3U},
      .workflow = {.timeout_ms = 5000, .retry_budget = 0U, .circuit_breaker_threshold = 3U},
      .max_tool_calls = max_tool_calls,
      .builtin_lane_enabled = true,
      .mcp_lane_enabled = true,
      .multi_agent_enabled = true,
      .capability_refresh_interval_ms = 30000,
      .capability_expire_after_ms = 180000,
      .stale_read_allowed = false,
      .capability_failure_backoff_ms = 5000,
      .snapshot_fingerprint = 7U,
  };
}

void test_builtin_lane_rejects_overflow_during_concurrent_acquire() {
  dasall::tools::execution::ExecutorLanePool pool;
  const auto timeout_view = make_timeout_view(1U);

  std::mutex gate_mutex;
  std::condition_variable gate_cv;
  bool release_gate = false;
  std::array<dasall::tools::execution::LaneAcquireResult, 2> results;

  std::array<std::thread, 2> workers{
      std::thread([&]() {
        std::unique_lock<std::mutex> lock(gate_mutex);
        gate_cv.wait(lock, [&]() { return release_gate; });
        lock.unlock();
        results[0] = pool.acquire_builtin(timeout_view);
      }),
      std::thread([&]() {
        std::unique_lock<std::mutex> lock(gate_mutex);
        gate_cv.wait(lock, [&]() { return release_gate; });
        lock.unlock();
        results[1] = pool.acquire_builtin(timeout_view);
      })};

  {
    std::lock_guard<std::mutex> lock(gate_mutex);
    release_gate = true;
  }
  gate_cv.notify_all();

  for (auto& worker : workers) {
    worker.join();
  }

  const auto acquired_count = std::count_if(
      results.begin(), results.end(), [](const auto& result) { return result.acquired; });
  const auto rejected_count = std::count_if(results.begin(), results.end(), [](const auto& result) {
    return !result.acquired && result.reason_code == "lane.builtin.backpressure";
  });

  assert_equal(1, acquired_count,
               "builtin lane should grant only one concurrent permit when max_tool_calls=1");
  assert_equal(1, rejected_count,
               "builtin lane overflow should return the stable backpressure reason code");

  for (const auto& result : results) {
    if (result.acquired) {
      pool.release(result.reservation.lane_key);
    }
  }
}

void test_release_restores_builtin_lane_capacity() {
  dasall::tools::execution::ExecutorLanePool pool;
  const auto timeout_view = make_timeout_view(2U);

  const auto first = pool.acquire_builtin(timeout_view);
  const auto second = pool.acquire_builtin(timeout_view);
  const auto third = pool.acquire_builtin(timeout_view);

  assert_true(first.acquired,
              "first builtin acquire should succeed while capacity is available");
  assert_true(second.acquired,
              "second builtin acquire should consume the remaining capacity");
  assert_true(!third.acquired,
              "third builtin acquire should fail after the lane budget is exhausted");
  assert_equal(std::string("lane.builtin.backpressure"), third.reason_code,
               "exhausted builtin lane should surface the builtin backpressure reason");

  pool.release(first.reservation.lane_key);

  const auto reopened = pool.acquire_builtin(timeout_view);
  assert_true(reopened.acquired,
              "releasing a builtin permit should restore the lane window for the next caller");

  pool.release(second.reservation.lane_key);
  pool.release(reopened.reservation.lane_key);
}

void test_mcp_lane_is_isolated_per_server() {
  dasall::tools::execution::ExecutorLanePool pool;
  const auto timeout_view = make_timeout_view(1U);

  const auto alpha = pool.acquire_mcp("mcp.alpha", timeout_view);
  const auto beta = pool.acquire_mcp("mcp.beta", timeout_view);
  const auto alpha_overflow = pool.acquire_mcp("mcp.alpha", timeout_view);

  assert_true(alpha.acquired,
              "first MCP acquire should succeed for the alpha server lane");
  assert_true(beta.acquired,
              "different MCP server ids should get isolated lane windows");
  assert_true(!alpha_overflow.acquired,
              "same MCP server lane should reject the second inflight request when saturated");
  assert_equal(std::string("lane.mcp.backpressure"), alpha_overflow.reason_code,
               "saturated MCP lane should return the MCP backpressure reason");

  pool.release(alpha.reservation.lane_key);
  pool.release(beta.reservation.lane_key);
}

}  // namespace

int main() {
  try {
    test_builtin_lane_rejects_overflow_during_concurrent_acquire();
    test_release_restores_builtin_lane_capacity();
    test_mcp_lane_is_isolated_per_server();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}