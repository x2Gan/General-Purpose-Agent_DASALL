// ==========================================================================
// MultiAgentBoundaryContracts.h
//
// WP04-T013-B: Multi-Agent boundary contracts aggregate entry point.
//
// This header is the single include for the ADR-008 direct-impact object
// catalog introduced at the start of the multi-agent wave. It does two things:
//   1. Centralizes the direct-impact object catalog for the four collaboration
//      subdomain objects frozen by WP04-T013-D:
//        - MultiAgentRequest
//        - MultiAgentResult
//        - WorkerTask
//        - WorkerLease
//   2. Re-exports the existing multi-agent boundary guards from
//      boundary/MultiAgentBoundaryGuards.h without redefining them.
//
// Scope discipline:
//   - T013 does NOT define MultiAgentRequest / MultiAgentResult / WorkerTask /
//     WorkerLease entities themselves; those belong to T014-T021.
//   - T013 does NOT introduce a WorkerLease guard; WorkerLease remains catalog-
//     only here and is implemented later by T020/T021.
//   - The aggregate header remains header-only and compile-time auditable.
//
// Design basis:
//   - ADR-008 §5.1: MultiAgentRequest is a collaboration-subdomain request and
//     must not reuse AgentRequest semantics.
//   - ADR-008 §5.2: MultiAgentResult is a collaboration result and must not
//     replace the top-level AgentResult.
//   - ADR-008 §5.3: WorkerTask is a subtask execution unit and must not carry
//     global Session/FSM control state.
//   - ADR-008 §5.4: WorkerLease is a lease-scope metadata object and must not
//     become a second top-level checkpoint or resume entry.
//   - WP04-T013-D: ADR-008 direct-impact object inventory.
//
// Consumers:
//   - tests/contract/smoke/MultiAgentBoundaryContractsSmokeTest.cpp
//   - Future WP04 multi-agent contract tests (T014-B through T021-B).
// ==========================================================================
#pragma once

#include <array>
#include <string_view>

#include "boundary/MultiAgentBoundaryGuards.h"

namespace dasall::contracts {

// MultiAgentBoundaryObject enumerates the four collaboration-subdomain objects
// introduced by ADR-008 into the WP04 multi-agent freezing wave.
enum class MultiAgentBoundaryObject {
  MultiAgentRequest,
  MultiAgentResult,
  WorkerTask,
  WorkerLease,
};

// MultiAgentBoundaryObjectEntry is the compile-time row representation used by
// smoke tests and later contract suites to audit ADR-008 object placement.
struct MultiAgentBoundaryObjectEntry {
  MultiAgentBoundaryObject object;
  std::string_view object_name;
  std::string_view upstream_anchor;
  std::string_view wp_range;
  std::string_view contract_focus;
  bool runtime_controlled;
};

// The catalog contains only the direct-impact multi-agent wave objects.
// AgentRequest / AgentResult / top-level checkpoint stay outside this catalog
// and act as upstream anchors, which keeps T013 aligned with its direct scope.
inline constexpr std::array<MultiAgentBoundaryObjectEntry, 4> kMultiAgentBoundaryObjectCatalog = {{
    {
        MultiAgentBoundaryObject::MultiAgentRequest,
        "MultiAgentRequest",
        "AgentRequest",
        "WP04-T014/T015",
        "collaboration-subdomain request only",
        true,
    },
    {
        MultiAgentBoundaryObject::MultiAgentResult,
        "MultiAgentResult",
        "AgentResult",
        "WP04-T016/T017",
        "collaboration result only",
        true,
    },
    {
        MultiAgentBoundaryObject::WorkerTask,
        "WorkerTask",
        "AgentOrchestrator task graph",
        "WP04-T018/T019",
        "worker execution unit only",
        true,
    },
    {
        MultiAgentBoundaryObject::WorkerLease,
        "WorkerLease",
        "WorkerTask / top-level checkpoint subdomain snapshot",
        "WP04-T020/T021",
        "worker lease metadata only",
        true,
    },
}};

constexpr std::string_view multi_agent_boundary_object_name(
    MultiAgentBoundaryObject object) {
  for (const auto& entry : kMultiAgentBoundaryObjectCatalog) {
    if (entry.object == object) {
      return entry.object_name;
    }
  }

  return "UnknownMultiAgentBoundaryObject";
}

constexpr std::string_view multi_agent_boundary_upstream_anchor(
    MultiAgentBoundaryObject object) {
  for (const auto& entry : kMultiAgentBoundaryObjectCatalog) {
    if (entry.object == object) {
      return entry.upstream_anchor;
    }
  }

  return "UnknownMultiAgentBoundaryAnchor";
}

constexpr bool is_multi_agent_boundary_object(std::string_view object_name) {
  for (const auto& entry : kMultiAgentBoundaryObjectCatalog) {
    if (entry.object_name == object_name) {
      return true;
    }
  }

  return false;
}

constexpr bool is_runtime_controlled_multi_agent_object(
    MultiAgentBoundaryObject object) {
  for (const auto& entry : kMultiAgentBoundaryObjectCatalog) {
    if (entry.object == object) {
      return entry.runtime_controlled;
    }
  }

  return false;
}

}  // namespace dasall::contracts