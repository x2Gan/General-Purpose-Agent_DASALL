#include "ToolConfigAdapter.h"

#include <functional>

namespace dasall::tools::config {

tools::ToolPolicyView ToolConfigAdapter::build_policy_view(
    const profiles::RuntimePolicySnapshot& snapshot,
    const profiles::BuildProfileManifest& manifest) const {
  const auto fingerprint = snapshot_fingerprint(snapshot);

  std::lock_guard<std::mutex> guard(cache_mutex_);
  if (cached_policy_view_.has_value() && cached_policy_fingerprint_ == fingerprint) {
    return *cached_policy_view_;
  }

  cached_policy_view_ = build_policy_view_uncached(snapshot, manifest);
  cached_policy_fingerprint_ = fingerprint;
  return *cached_policy_view_;
}

ToolTimeoutView ToolConfigAdapter::build_timeout_view(
    const profiles::RuntimePolicySnapshot& snapshot,
    const profiles::BuildProfileManifest& manifest) const {
  const auto fingerprint = snapshot_fingerprint(snapshot);

  std::lock_guard<std::mutex> guard(cache_mutex_);
  if (cached_timeout_view_.has_value() && cached_timeout_fingerprint_ == fingerprint) {
    return *cached_timeout_view_;
  }

  cached_timeout_view_ = build_timeout_view_uncached(snapshot, manifest);
  cached_timeout_fingerprint_ = fingerprint;
  return *cached_timeout_view_;
}

bool ToolConfigAdapter::is_snapshot_current(
    std::uint64_t fingerprint,
    const profiles::RuntimePolicySnapshot& snapshot) const {
  return fingerprint == snapshot_fingerprint(snapshot);
}

std::uint64_t ToolConfigAdapter::snapshot_fingerprint(
    const profiles::RuntimePolicySnapshot& snapshot) const {
  return snapshot.generation() ^
         (static_cast<std::uint64_t>(std::hash<std::string>{}(snapshot.effective_profile_id()))
          << 1U);
}

tools::ToolPolicyView ToolConfigAdapter::build_policy_view_uncached(
    const profiles::RuntimePolicySnapshot& snapshot,
    const profiles::BuildProfileManifest& manifest) const {
  if (!snapshot.has_consistent_values() || !manifest.has_consistent_values()) {
    return tools::ToolPolicyView{
        .effective_profile_id = snapshot.effective_profile_id(),
        .safe_mode_enabled = true,
        .high_risk_confirmation_required = true,
        .audit_level = {},
        .allowed_tool_domains = {},
        .tool_visibility_rules = {},
    };
  }

  return tools::ToolPolicyView{
      .effective_profile_id = snapshot.effective_profile_id(),
      .safe_mode_enabled = snapshot.execution_policy().safe_mode_enabled,
      .high_risk_confirmation_required =
          snapshot.execution_policy().requires_high_risk_confirmation,
      .audit_level = snapshot.execution_policy().audit_level,
      .allowed_tool_domains = snapshot.execution_policy().allowed_tool_domains,
      .tool_visibility_rules = snapshot.prompt_policy().tool_visibility_rules,
  };
}

ToolTimeoutView ToolConfigAdapter::build_timeout_view_uncached(
    const profiles::RuntimePolicySnapshot& snapshot,
    const profiles::BuildProfileManifest& manifest) const {
  ToolTimeoutView deny_view{
      .snapshot_fingerprint = snapshot_fingerprint(snapshot),
  };
  if (!snapshot.has_consistent_values() || !manifest.has_consistent_values()) {
    return deny_view;
  }

  return ToolTimeoutView{
      .tool = ToolLaneTimeoutBudget{
          .timeout_ms = snapshot.timeout_policy().tool.timeout_ms,
          .retry_budget = snapshot.timeout_policy().tool.retry_budget,
          .circuit_breaker_threshold = snapshot.timeout_policy().tool.circuit_breaker_threshold,
      },
      .mcp = ToolLaneTimeoutBudget{
          .timeout_ms = snapshot.timeout_policy().mcp.timeout_ms,
          .retry_budget = snapshot.timeout_policy().mcp.retry_budget,
          .circuit_breaker_threshold = snapshot.timeout_policy().mcp.circuit_breaker_threshold,
      },
      .workflow = ToolLaneTimeoutBudget{
          .timeout_ms = snapshot.timeout_policy().workflow.timeout_ms,
          .retry_budget = snapshot.timeout_policy().workflow.retry_budget,
          .circuit_breaker_threshold =
              snapshot.timeout_policy().workflow.circuit_breaker_threshold,
      },
      .max_tool_calls = snapshot.runtime_budget().max_tool_calls.value_or(0U),
      .builtin_lane_enabled = manifest.enables_module("tools_builtin"),
      .mcp_lane_enabled = manifest.enables_module("tools_mcp"),
      .multi_agent_enabled = manifest.enables_module("multi_agent"),
      .capability_refresh_interval_ms =
          snapshot.capability_cache_policy().refresh_interval_ms,
      .capability_expire_after_ms = snapshot.capability_cache_policy().expire_after_ms,
      .stale_read_allowed = snapshot.capability_cache_policy().stale_read_allowed,
      .capability_failure_backoff_ms =
          snapshot.capability_cache_policy().failure_backoff_ms,
      .snapshot_fingerprint = snapshot_fingerprint(snapshot),
  };
}

}  // namespace dasall::tools::config