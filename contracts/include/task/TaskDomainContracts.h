// ============================================================================
// TaskDomainContracts.h
//
// WP05-T008-B: task subdomain aggregate contracts entry.
//
// This header provides:
//   1. The object catalog for task-subdomain contracts in Wave3.
//   2. A minimal SubTaskGraph snapshot object used by task-domain consumers.
//   3. Lookup helpers for object names, upstream anchors, and runtime ownership.
//
// Scope discipline:
//   - The catalog keeps only task-domain objects and does not duplicate top-level
//     AgentRequest/AgentResult/Checkpoint entities.
//   - SubTaskGraph is intentionally modeled as a compact snapshot object and does
//     not encode scheduling algorithms or runtime policy internals.
// ============================================================================
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dasall::contracts {

// TaskDomainObject enumerates the currently frozen task-subdomain contract
// objects in WP05-T008.
enum class TaskDomainObject {
  WorkerTask,
  WorkerLease,
  SubTaskGraph,
};

// TaskDomainObjectEntry stores one catalog row for compile-time auditing in
// contract tests.
struct TaskDomainObjectEntry {
  TaskDomainObject object;
  std::string_view object_name;
  std::string_view upstream_anchor;
  std::string_view wp_range;
  std::string_view contract_focus;
  bool runtime_controlled;
};

// SubTaskGraph is the minimal collaboration subtask-graph snapshot object.
// It captures graph identity and node anchors without carrying top-level
// Session/FSM state or scheduler internals.
struct SubTaskGraph {
  // Stable identity for the current subtask-graph snapshot.
  std::optional<std::string> graph_id;

  // Root task anchor linked to orchestrator-owned task lineage.
  std::optional<std::string> root_task_id;

  // Compact list of subtask ids included in the snapshot.
  std::optional<std::vector<std::string>> task_ids;

  // Optional monotonic revision marker for snapshot lineage.
  std::optional<std::uint32_t> graph_revision;
};

// Catalog of task-domain contract objects frozen by WP05-T008.
inline constexpr std::array<TaskDomainObjectEntry, 3> kTaskDomainObjectCatalog = {{
    {
        TaskDomainObject::WorkerTask,
        "WorkerTask",
        "AgentOrchestrator task graph",
        "WP04-T018/T019",
        "worker execution unit only",
        true,
    },
    {
        TaskDomainObject::WorkerLease,
        "WorkerLease",
        "WorkerTask / top-level checkpoint subdomain snapshot",
        "WP04-T020/T021",
        "worker lease metadata only",
        true,
    },
    {
        TaskDomainObject::SubTaskGraph,
        "SubTaskGraph",
        "MultiAgentCoordinator collaboration subgraph",
        "WP05-T008",
        "subtask graph snapshot only",
        true,
    },
}};

// Returns the stable object name used in tests and diagnostics.
constexpr std::string_view task_domain_object_name(TaskDomainObject object) {
  for (const auto& entry : kTaskDomainObjectCatalog) {
    if (entry.object == object) {
      return entry.object_name;
    }
  }

  return "UnknownTaskDomainObject";
}

// Returns the frozen upstream anchor for one task-domain object.
constexpr std::string_view task_domain_upstream_anchor(TaskDomainObject object) {
  for (const auto& entry : kTaskDomainObjectCatalog) {
    if (entry.object == object) {
      return entry.upstream_anchor;
    }
  }

  return "UnknownTaskDomainAnchor";
}

// Checks whether a candidate name belongs to the task-domain catalog.
constexpr bool is_task_domain_object(std::string_view object_name) {
  for (const auto& entry : kTaskDomainObjectCatalog) {
    if (entry.object_name == object_name) {
      return true;
    }
  }

  return false;
}

// Reports whether one task-domain object is controlled by runtime orchestration.
constexpr bool is_runtime_controlled_task_domain_object(TaskDomainObject object) {
  for (const auto& entry : kTaskDomainObjectCatalog) {
    if (entry.object == object) {
      return entry.runtime_controlled;
    }
  }

  return false;
}

}  // namespace dasall::contracts
