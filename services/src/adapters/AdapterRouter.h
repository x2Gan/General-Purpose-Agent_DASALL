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

struct ServicePolicyView {
  bool local_platform_route_enabled = false;
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

}  // namespace dasall::services::internal