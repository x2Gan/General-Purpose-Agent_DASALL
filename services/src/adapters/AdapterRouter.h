#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dasall::services::internal {

enum class AdapterRouteKind {
  local_platform,
  local_service,
  remote_service,
};

enum class AdapterTrustClass {
  untrusted,
  caller_verified,
  trusted_local,
};

enum class AdapterAvailabilityState {
  available,
  degraded,
  unavailable,
  unknown,
};

enum class AdapterRouteRequestKind {
  action,
  query,
};

enum class AdapterRouteFailure {
  invalid_request,
  capability_unsupported,
  route_unavailable,
  fallback_blocked,
  route_not_permitted,
};

enum class ServiceQueueOverflowPolicy {
  reject,
  drop_oldest,
};

struct ServicePolicyView {
  std::string effective_profile_id;
  std::uint32_t command_lane_workers = 1U;
  std::uint32_t execution_query_lane_workers = 1U;
  std::uint32_t data_query_lane_workers = 1U;
  std::int64_t request_deadline_ceiling_ms = 0;
  std::int64_t adapter_call_timeout_ms = 0;
  std::int64_t orchestration_timeout_ms = 0;
  std::uint32_t adapter_failure_threshold = 0U;
  std::int64_t data_cache_ttl_ms = 0;
  bool default_allow_stale_reads = false;
  std::int64_t resync_backoff_ms = 0;
  ServiceQueueOverflowPolicy command_queue_overflow_policy =
      ServiceQueueOverflowPolicy::reject;
  ServiceQueueOverflowPolicy subscription_queue_overflow_policy =
      ServiceQueueOverflowPolicy::drop_oldest;
  bool read_path_degrade_allowed = false;
  bool high_risk_confirmation_required = true;
  bool safe_mode_enabled = true;
  std::string audit_level;
  std::vector<std::string> caller_domain_allowlist;
  bool local_platform_route_enabled = false;
  bool observability_bridge_enabled = false;
  std::string metrics_granularity;
  double trace_sample_ratio = 0.0;
  bool remote_diagnostics_enabled = false;
  std::vector<AdapterRouteKind> adapter_preference_order;
};

struct CapabilitySnapshotView {
  std::string capability_id;
  std::string capability_version;
  std::vector<std::string> supported_actions;
  std::vector<std::string> supported_queries;
  std::vector<AdapterRouteKind> route_classes;
  std::optional<AdapterRouteKind> preferred_locality;
};

struct FallbackEnvelope {
  std::string requested_action_class;
  std::vector<AdapterRouteKind> ordered_candidates;
  std::string route_equivalence_class;
  bool allow_degrade = false;
  std::string deny_reason_on_exhaustion;
};

struct AdapterCandidateView {
  std::string adapter_id;
  AdapterRouteKind route_kind = AdapterRouteKind::local_service;
  std::string route_equivalence_class;
  AdapterTrustClass trust_class = AdapterTrustClass::untrusted;
  AdapterAvailabilityState availability_state = AdapterAvailabilityState::unknown;
  std::vector<std::string> supported_capabilities;
};

struct AdapterSelection {
  AdapterRouteKind route_kind = AdapterRouteKind::local_service;
  std::string adapter_id;
  std::string target_id;
  std::string route_equivalence_class;
  std::uint32_t fallback_hop = 0U;
  std::string selected_reason;
  AdapterTrustClass trust_class = AdapterTrustClass::untrusted;
  AdapterAvailabilityState availability_state = AdapterAvailabilityState::unknown;
};

struct AdapterRouteRequest {
  std::string capability_id;
  std::string target_id;
  AdapterRouteRequestKind request_kind = AdapterRouteRequestKind::action;
  std::string requested_operation;
  bool high_risk = false;
  AdapterTrustClass minimum_trust = AdapterTrustClass::untrusted;
  ServicePolicyView policy_view;
  CapabilitySnapshotView capability_snapshot;
  FallbackEnvelope fallback_envelope;
  std::vector<AdapterCandidateView> registered_candidates;
};

struct AdapterRouteDecision {
  std::optional<AdapterSelection> selection;
  AdapterRouteFailure failure = AdapterRouteFailure::route_unavailable;
  std::string reason;

  [[nodiscard]] bool ok() const {
    return selection.has_value();
  }
};

class AdapterRouter {
 public:
  [[nodiscard]] AdapterRouteDecision select_adapter(const AdapterRouteRequest& request) const;
};

[[nodiscard]] std::string_view route_kind_name(AdapterRouteKind route_kind);
[[nodiscard]] std::string_view route_failure_name(AdapterRouteFailure failure);
[[nodiscard]] std::string_view overflow_policy_name(ServiceQueueOverflowPolicy overflow_policy);

}  // namespace dasall::services::internal