#pragma once

#include <string_view>

#include "boundary/ObjectBoundaryCatalog.h"

namespace dasall::contracts {

// BoundaryGuardDecision classifies the stable-boundary admission outcome.
// The decision values map directly to WP01 boundary governance semantics:
// - AllowStable: object is stable and can enter stable contracts.
// - RejectBlocked: object is in the blocked set and must not leak outward.
// - RejectDeferred: object is deferred and cannot enter stable contracts yet.
enum class BoundaryGuardDecision {
  AllowStable,
  RejectBlocked,
  RejectDeferred,
};

// BoundaryGuardResult carries a binary admission flag plus a normalized
// decision and reason string, so CI and contract tests can assert behavior
// without duplicating category-to-reason mapping logic.
struct BoundaryGuardResult {
  bool allowed = false;
  BoundaryGuardDecision decision = BoundaryGuardDecision::RejectBlocked;
  std::string_view reason = "blocked objects cannot enter stable contracts";
};

// Evaluates whether an object is allowed to enter the stable contracts set.
// This function intentionally operates only on object-level categories and
// does not inspect any fields, preserving WP-01 object-boundary scope.
constexpr BoundaryGuardResult evaluate_stable_boundary(ContractObject object) {
  switch (boundary_category(object)) {
    case BoundaryCategory::Stable:
      return BoundaryGuardResult{
          .allowed = true,
          .decision = BoundaryGuardDecision::AllowStable,
          .reason = "stable objects can enter stable contracts",
      };

    case BoundaryCategory::Blocked:
      return BoundaryGuardResult{
          .allowed = false,
          .decision = BoundaryGuardDecision::RejectBlocked,
          .reason = "blocked objects cannot enter stable contracts",
      };

    case BoundaryCategory::Deferred:
      return BoundaryGuardResult{
          .allowed = false,
          .decision = BoundaryGuardDecision::RejectDeferred,
          .reason = "deferred objects cannot enter stable contracts in WP01",
      };
  }

  // Defensive fallback for forward compatibility if enum categories expand.
  return BoundaryGuardResult{};
}

// Helper for callers that only need a boolean admission result.
constexpr bool can_enter_stable_boundary(ContractObject object) {
  return evaluate_stable_boundary(object).allowed;
}

}  // namespace dasall::contracts
