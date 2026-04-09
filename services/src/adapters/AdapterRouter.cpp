#include "adapters/AdapterRouter.h"

#include <algorithm>

namespace dasall::services::internal {

namespace {

[[nodiscard]] bool contains_string(const std::vector<std::string>& values, std::string_view target) {
  return std::any_of(values.begin(), values.end(), [target](const std::string& value) {
    return value == target;
  });
}

[[nodiscard]] bool contains_route_kind(const std::vector<AdapterRouteKind>& values,
                                       AdapterRouteKind target) {
  return std::find(values.begin(), values.end(), target) != values.end();
}

void append_unique(std::vector<AdapterRouteKind>& values, AdapterRouteKind target) {
  if (!contains_route_kind(values, target)) {
    values.push_back(target);
  }
}

[[nodiscard]] int trust_rank(AdapterTrustClass trust_class) {
  switch (trust_class) {
    case AdapterTrustClass::trusted_local:
      return 3;
    case AdapterTrustClass::caller_verified:
      return 2;
    case AdapterTrustClass::untrusted:
      return 1;
  }

  return 0;
}

[[nodiscard]] int availability_rank(AdapterAvailabilityState availability_state) {
  switch (availability_state) {
    case AdapterAvailabilityState::available:
      return 3;
    case AdapterAvailabilityState::degraded:
      return 2;
    case AdapterAvailabilityState::unavailable:
      return 0;
    case AdapterAvailabilityState::unknown:
      return 0;
  }

  return 0;
}

[[nodiscard]] bool operation_supported(const AdapterRouteRequest& request) {
  if (request.request_kind == AdapterRouteRequestKind::action) {
    return contains_string(request.capability_snapshot.supported_actions,
                           request.requested_operation);
  }

  return contains_string(request.capability_snapshot.supported_queries,
                         request.requested_operation);
}

[[nodiscard]] std::vector<AdapterRouteKind> build_route_order(const AdapterRouteRequest& request) {
  std::vector<AdapterRouteKind> route_order;

  if (!request.fallback_envelope.ordered_candidates.empty()) {
    route_order = request.fallback_envelope.ordered_candidates;
    return route_order;
  }

  if (request.capability_snapshot.preferred_locality.has_value()) {
    append_unique(route_order, *request.capability_snapshot.preferred_locality);
  }

  for (const auto route_kind : request.policy_view.adapter_preference_order) {
    append_unique(route_order, route_kind);
  }

  for (const auto route_kind : request.capability_snapshot.route_classes) {
    append_unique(route_order, route_kind);
  }

  return route_order;
}

[[nodiscard]] AdapterRouteDecision make_failure(AdapterRouteFailure failure,
                                                const std::string& reason) {
  return AdapterRouteDecision{
      .selection = std::nullopt,
      .failure = failure,
      .reason = reason,
  };
}

}  // namespace

std::string_view route_kind_name(AdapterRouteKind route_kind) {
  switch (route_kind) {
    case AdapterRouteKind::local_platform:
      return "local_platform";
    case AdapterRouteKind::local_service:
      return "local_service";
    case AdapterRouteKind::remote_service:
      return "remote_service";
  }

  return "unknown_route_kind";
}

std::string_view route_failure_name(AdapterRouteFailure failure) {
  switch (failure) {
    case AdapterRouteFailure::invalid_request:
      return "invalid_request";
    case AdapterRouteFailure::capability_unsupported:
      return "capability_unsupported";
    case AdapterRouteFailure::route_unavailable:
      return "route_unavailable";
    case AdapterRouteFailure::fallback_blocked:
      return "fallback_blocked";
    case AdapterRouteFailure::route_not_permitted:
      return "route_not_permitted";
  }

  return "unknown_route_failure";
}

AdapterRouteDecision AdapterRouter::select_adapter(const AdapterRouteRequest& request) const {
  if (request.capability_id.empty() || request.target_id.empty() ||
      request.requested_operation.empty()) {
    return make_failure(AdapterRouteFailure::invalid_request,
                        "capability_id, target_id, and requested_operation are required");
  }

  if (request.capability_snapshot.capability_id != request.capability_id) {
    return make_failure(AdapterRouteFailure::capability_unsupported,
                        "capability snapshot does not match the requested capability_id");
  }

  if (!operation_supported(request)) {
    return make_failure(AdapterRouteFailure::capability_unsupported,
                        "requested operation is not declared in CapabilitySnapshotView");
  }

  const auto route_order = build_route_order(request);
  if (route_order.empty()) {
    return make_failure(AdapterRouteFailure::route_unavailable,
                        "no route candidates are available in policy or fallback envelope");
  }

  bool saw_supported_route = false;
  bool saw_candidate_for_supported_route = false;
  bool saw_trust_rejection = false;
  bool saw_equivalence_rejection = false;
  bool saw_availability_rejection = false;
  bool saw_no_degrade_block = false;

  for (std::size_t route_index = 0; route_index < route_order.size(); ++route_index) {
    const auto route_kind = route_order[route_index];

    if (!contains_route_kind(request.capability_snapshot.route_classes, route_kind)) {
      continue;
    }

    saw_supported_route = true;

    if (route_kind == AdapterRouteKind::local_platform &&
        !request.policy_view.local_platform_route_enabled) {
      continue;
    }

    if (route_index > 0U && !request.fallback_envelope.allow_degrade) {
      saw_no_degrade_block = true;
      break;
    }

    const AdapterCandidateView* best_candidate = nullptr;
    int best_availability_rank = -1;

    for (const auto& candidate : request.registered_candidates) {
      if (candidate.route_kind != route_kind) {
        continue;
      }

      if (!contains_string(candidate.supported_capabilities, request.capability_id)) {
        continue;
      }

      saw_candidate_for_supported_route = true;

      if (!request.fallback_envelope.route_equivalence_class.empty() &&
          candidate.route_equivalence_class != request.fallback_envelope.route_equivalence_class) {
        saw_equivalence_rejection = true;
        continue;
      }

      if (trust_rank(candidate.trust_class) < trust_rank(request.minimum_trust)) {
        saw_trust_rejection = true;
        continue;
      }

      const auto candidate_availability_rank = availability_rank(candidate.availability_state);
      if (candidate_availability_rank == 0) {
        saw_availability_rejection = true;
        continue;
      }

      if (best_candidate == nullptr || candidate_availability_rank > best_availability_rank) {
        best_candidate = &candidate;
        best_availability_rank = candidate_availability_rank;
      }
    }

    if (best_candidate == nullptr) {
      continue;
    }

    const auto selection_reason = route_index == 0U ? "preferred_route_selected"
                                                     : "fallback_route_selected";

    return AdapterRouteDecision{
        .selection = AdapterSelection{
            .route_kind = best_candidate->route_kind,
            .adapter_id = best_candidate->adapter_id,
            .target_id = request.target_id,
            .route_equivalence_class = best_candidate->route_equivalence_class,
            .fallback_hop = static_cast<std::uint32_t>(route_index),
            .selected_reason = selection_reason,
            .trust_class = best_candidate->trust_class,
            .availability_state = best_candidate->availability_state,
        },
        .failure = AdapterRouteFailure::route_unavailable,
        .reason = std::string(selection_reason),
    };
  }

  if (saw_no_degrade_block || saw_equivalence_rejection) {
    const auto reason = request.fallback_envelope.deny_reason_on_exhaustion.empty()
                            ? std::string("fallback is blocked outside the runtime envelope")
                            : request.fallback_envelope.deny_reason_on_exhaustion;
    return make_failure(AdapterRouteFailure::fallback_blocked, reason);
  }

  if (saw_trust_rejection) {
    return make_failure(AdapterRouteFailure::route_not_permitted,
                        "candidate trust_class is below the required route trust");
  }

  if (saw_supported_route &&
      (saw_candidate_for_supported_route || saw_availability_rejection || request.high_risk)) {
    return make_failure(AdapterRouteFailure::route_unavailable,
                        "supported routes exist but no candidate is currently selectable");
  }

  return make_failure(AdapterRouteFailure::capability_unsupported,
                      "CapabilitySnapshotView does not advertise any eligible route_kind");
}

}  // namespace dasall::services::internal