#include "ops/ServiceConfigAdapter.h"

#include <algorithm>
#include <array>
#include <string_view>

#include "BuildProfileManifest.h"
#include "RuntimePolicySnapshot.h"

namespace dasall::services::internal {

namespace {

[[nodiscard]] bool is_supported_audit_level(const std::string& audit_level) {
  static constexpr std::array<std::string_view, 2> kSupportedLevels = {
      "full",
      "standard",
  };
  return std::find(kSupportedLevels.begin(), kSupportedLevels.end(), audit_level) !=
         kSupportedLevels.end();
}

[[nodiscard]] bool is_supported_metrics_granularity(const std::string& granularity) {
  static constexpr std::array<std::string_view, 3> kSupportedLevels = {
      "full",
      "partial",
      "minimal",
  };
  return std::find(kSupportedLevels.begin(), kSupportedLevels.end(), granularity) !=
         kSupportedLevels.end();
}

}  // namespace

ServicePolicyDerivationResult ServiceConfigAdapter::derive_policy_view(
    const profiles::RuntimePolicySnapshot& runtime_policy,
    const profiles::BuildProfileManifest& build_manifest) const {
  if (!runtime_policy.has_consistent_values()) {
    return ServicePolicyDerivationResult{
        .policy_view = std::nullopt,
        .error = "runtime policy snapshot is inconsistent",
    };
  }

  if (!build_manifest.has_consistent_values()) {
    return ServicePolicyDerivationResult{
        .policy_view = std::nullopt,
        .error = "build profile manifest is inconsistent",
    };
  }

  if (!is_supported_audit_level(runtime_policy.execution_policy().audit_level)) {
    return ServicePolicyDerivationResult{
        .policy_view = std::nullopt,
        .error = "execution_policy.audit_level is not supported by services",
    };
  }

  if (!is_supported_metrics_granularity(runtime_policy.ops_policy().metrics_granularity)) {
    return ServicePolicyDerivationResult{
        .policy_view = std::nullopt,
        .error = "ops_policy.metrics_granularity is not supported by services",
    };
  }

  const auto worker_threads = runtime_policy.worker_threads();

  return ServicePolicyDerivationResult{
      .policy_view = ServicePolicyView{
          .effective_profile_id = runtime_policy.effective_profile_id(),
          .command_lane_workers = derive_command_lane_workers(worker_threads),
          .execution_query_lane_workers = derive_query_lane_workers(worker_threads),
          .data_query_lane_workers = derive_query_lane_workers(worker_threads),
          .request_deadline_ceiling_ms = static_cast<std::int64_t>(
              runtime_policy.runtime_budget().max_latency_ms.value_or(0U)),
          .adapter_call_timeout_ms = runtime_policy.timeout_policy().tool.timeout_ms,
          .orchestration_timeout_ms = runtime_policy.timeout_policy().workflow.timeout_ms,
          .adapter_failure_threshold =
              runtime_policy.timeout_policy().tool.circuit_breaker_threshold,
          .data_cache_ttl_ms = runtime_policy.capability_cache_policy().expire_after_ms,
          .default_allow_stale_reads =
              runtime_policy.capability_cache_policy().stale_read_allowed,
          .resync_backoff_ms = runtime_policy.capability_cache_policy().failure_backoff_ms,
          .command_queue_overflow_policy = ServiceQueueOverflowPolicy::reject,
          .subscription_queue_overflow_policy = ServiceQueueOverflowPolicy::drop_oldest,
          .read_path_degrade_allowed = runtime_policy.degrade_policy().allow_budget_degrade,
          .high_risk_confirmation_required =
              runtime_policy.execution_policy().requires_high_risk_confirmation,
          .safe_mode_enabled = runtime_policy.execution_policy().safe_mode_enabled,
          .audit_level = runtime_policy.execution_policy().audit_level,
          .local_platform_route_enabled = build_manifest.enables_module("platform_hal"),
          .observability_bridge_enabled =
              build_manifest.enables_module("infra_observability") &&
              build_manifest.observability_level != "minimal",
          .metrics_granularity = runtime_policy.ops_policy().metrics_granularity,
          .trace_sample_ratio = runtime_policy.ops_policy().trace_sample_ratio,
          .remote_diagnostics_enabled =
              runtime_policy.ops_policy().remote_diagnostics_enabled,
          .adapter_preference_order = derive_adapter_preference_order(build_manifest),
      },
      .error = {},
  };
}

std::vector<AdapterRouteKind> ServiceConfigAdapter::derive_adapter_preference_order(
    const profiles::BuildProfileManifest& build_manifest) const {
  std::vector<AdapterRouteKind> route_order;
  if (build_manifest.enables_module("platform_hal")) {
    route_order.push_back(AdapterRouteKind::local_platform);
  }

  route_order.push_back(AdapterRouteKind::local_service);
  route_order.push_back(AdapterRouteKind::remote_service);
  return route_order;
}

std::uint32_t ServiceConfigAdapter::derive_command_lane_workers(
    std::uint32_t worker_threads) const {
  return std::min<std::uint32_t>(4U, std::max<std::uint32_t>(1U, worker_threads / 3U));
}

std::uint32_t ServiceConfigAdapter::derive_query_lane_workers(
    std::uint32_t worker_threads) const {
  return std::max<std::uint32_t>(1U, worker_threads / 4U);
}

}  // namespace dasall::services::internal