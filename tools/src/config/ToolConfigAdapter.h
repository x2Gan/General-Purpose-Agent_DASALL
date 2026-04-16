#pragma once

#include <cstdint>
#include <mutex>
#include <optional>

#include "BuildProfileManifest.h"
#include "IPolicyGate.h"
#include "RuntimePolicySnapshot.h"

namespace dasall::tools::config {

struct ToolLaneTimeoutBudget {
  std::int64_t timeout_ms = 0;
  std::uint32_t retry_budget = 0U;
  std::uint32_t circuit_breaker_threshold = 0U;
};

struct ToolTimeoutView {
  ToolLaneTimeoutBudget tool;
  ToolLaneTimeoutBudget mcp;
  ToolLaneTimeoutBudget workflow;
  std::uint32_t max_tool_calls = 0U;
  bool builtin_lane_enabled = false;
  bool mcp_lane_enabled = false;
  bool multi_agent_enabled = false;
  std::int64_t capability_refresh_interval_ms = 0;
  std::int64_t capability_expire_after_ms = 0;
  bool stale_read_allowed = false;
  std::int64_t capability_failure_backoff_ms = 0;
  std::uint64_t snapshot_fingerprint = 0U;
};

class ToolConfigAdapter {
 public:
  [[nodiscard]] tools::ToolPolicyView build_policy_view(
      const profiles::RuntimePolicySnapshot& snapshot,
      const profiles::BuildProfileManifest& manifest) const;
  [[nodiscard]] ToolTimeoutView build_timeout_view(
      const profiles::RuntimePolicySnapshot& snapshot,
      const profiles::BuildProfileManifest& manifest) const;
  [[nodiscard]] bool is_snapshot_current(
      std::uint64_t fingerprint,
      const profiles::RuntimePolicySnapshot& snapshot) const;
  [[nodiscard]] std::uint64_t snapshot_fingerprint(
      const profiles::RuntimePolicySnapshot& snapshot) const;

 private:
  [[nodiscard]] tools::ToolPolicyView build_policy_view_uncached(
      const profiles::RuntimePolicySnapshot& snapshot,
      const profiles::BuildProfileManifest& manifest) const;
  [[nodiscard]] ToolTimeoutView build_timeout_view_uncached(
      const profiles::RuntimePolicySnapshot& snapshot,
      const profiles::BuildProfileManifest& manifest) const;

  mutable std::mutex cache_mutex_;
  mutable std::uint64_t cached_policy_fingerprint_ = 0U;
  mutable std::optional<tools::ToolPolicyView> cached_policy_view_;
  mutable std::uint64_t cached_timeout_fingerprint_ = 0U;
  mutable std::optional<ToolTimeoutView> cached_timeout_view_;
};

}  // namespace dasall::tools::config