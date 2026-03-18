// ==========================================================================
// RecoveryBoundaryContracts.h
//
// WP04-T006-B: Recovery boundary contracts aggregate entry point.
//
// This header is the single include for the Recovery subdomain boundary
// catalog introduced in WP04. It does two things only:
//   1. Centralizes the ADR-007 direct-impact object list for the Recovery
//      chain: ReflectionDecision, RecoveryRequest, RecoveryOutcome.
//   2. Re-exports the already-frozen Recovery boundary guards from
//      boundary/RecoveryBoundaryGuards.h without redefining them.
//
// Scope discipline:
//   - T006 does NOT define ReflectionDecision / RecoveryRequest /
//     RecoveryOutcome objects themselves; those belong to T007-T012.
//   - T006 does NOT redefine Checkpoint; Checkpoint is an M3 baseline anchor
//     object consumed by RecoveryRequest and RecoveryOutcome.
//   - The aggregate header remains header-only and compile-time auditable.
//
// Design basis:
//   - ADR-007 §5.1: ReflectionDecision suggestion-only boundary.
//   - ADR-007 §5.2: RecoveryRequest runtime-admission input boundary.
//   - ADR-007 §5.3: RecoveryOutcome execution-result boundary.
//   - WP04-T006-D: ADR-007 object impact inventory.
//
// Consumers:
//   - tests/contract/smoke/RecoveryBoundaryContractsSmokeTest.cpp
//   - Future WP04 recovery contract tests (T007-B through T012-B).
// ==========================================================================
#pragma once

#include <array>
#include <string_view>

#include "boundary/RecoveryBoundaryGuards.h"

namespace dasall::contracts {

// RecoveryBoundaryObject enumerates the three Recovery-chain objects that are
// directly introduced by ADR-007 into the WP04 recovery wave.
enum class RecoveryBoundaryObject {
  ReflectionDecision,
  RecoveryRequest,
  RecoveryOutcome,
};

// RecoveryBoundaryLayer names the architectural layer that owns the object.
enum class RecoveryBoundaryLayer {
  Cognition,
  Runtime,
};

// RecoveryBoundaryObjectEntry is the compile-time row representation used by
// smoke tests and future recovery contract tests.
struct RecoveryBoundaryObjectEntry {
  RecoveryBoundaryObject object;
  std::string_view object_name;
  RecoveryBoundaryLayer owner_layer;
  std::string_view wp_range;
  std::string_view contract_focus;
};

// The catalog intentionally contains only the three T006 direct-impact
// objects. Checkpoint remains an upstream dependency and is not duplicated
// here to keep the aggregate entry aligned with WP04-T006 scope.
inline constexpr std::array<RecoveryBoundaryObjectEntry, 3> kRecoveryBoundaryObjectCatalog = {{
    {
        RecoveryBoundaryObject::ReflectionDecision,
        "ReflectionDecision",
        RecoveryBoundaryLayer::Cognition,
        "WP04-T007/T008",
        "failure-semantics suggestion only",
    },
    {
        RecoveryBoundaryObject::RecoveryRequest,
        "RecoveryRequest",
        RecoveryBoundaryLayer::Runtime,
        "WP04-T009/T010",
        "runtime admission input only",
    },
    {
        RecoveryBoundaryObject::RecoveryOutcome,
        "RecoveryOutcome",
        RecoveryBoundaryLayer::Runtime,
        "WP04-T011/T012",
        "execution result and control metadata only",
    },
}};

constexpr std::string_view recovery_boundary_object_name(RecoveryBoundaryObject object) {
  for (const auto& entry : kRecoveryBoundaryObjectCatalog) {
    if (entry.object == object) {
      return entry.object_name;
    }
  }

  return "UnknownRecoveryBoundaryObject";
}

constexpr RecoveryBoundaryLayer recovery_boundary_owner_layer(RecoveryBoundaryObject object) {
  for (const auto& entry : kRecoveryBoundaryObjectCatalog) {
    if (entry.object == object) {
      return entry.owner_layer;
    }
  }

  return RecoveryBoundaryLayer::Runtime;
}

constexpr bool is_recovery_boundary_object(std::string_view object_name) {
  for (const auto& entry : kRecoveryBoundaryObjectCatalog) {
    if (entry.object_name == object_name) {
      return true;
    }
  }

  return false;
}

constexpr bool is_cognition_owned_recovery_object(RecoveryBoundaryObject object) {
  return recovery_boundary_owner_layer(object) == RecoveryBoundaryLayer::Cognition;
}

constexpr bool is_runtime_owned_recovery_object(RecoveryBoundaryObject object) {
  return recovery_boundary_owner_layer(object) == RecoveryBoundaryLayer::Runtime;
}

}  // namespace dasall::contracts