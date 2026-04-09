#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace dasall::contracts {

// InterfaceCandidate enumerates the first-pass stage-5 contract-interface
// candidates that survive the "stable dependency surface only" filter. The
// catalog intentionally excludes platform, infra, and protocol-internal
// interfaces because they are not shared contracts between top-level modules.
enum class InterfaceCandidate {
  IPlanner,
  IToolManager,
  ILLMAdapter,
  IMemoryStore,
  IContextOrchestrator,
  IKnowledgeService,
  IExecutionService,
  IDataService,
  IAgentRegistry,
  IResultMerger,
};

// InterfaceOwnerModule records the top-level module that owns the contract
// interface implementation or authoritative abstraction boundary.
enum class InterfaceOwnerModule {
  Cognition,
  Tools,
  Llm,
  Memory,
  Knowledge,
  Services,
  MultiAgent,
};

// InterfacePrimaryConsumer captures the first consumer side that relies on the
// interface as a cross-module dependency inversion point.
enum class InterfacePrimaryConsumer {
  Runtime,
  Tools,
};

// InterfaceAdmissionReadiness tracks whether the candidate already has enough
// frozen supporting contracts to enter the next admission-review task.
enum class InterfaceAdmissionReadiness {
  ReviewReady,
  AwaitingSupportingContracts,
};

// InterfaceCatalogEntry is the canonical compile-time row consumed by contract
// tests and by the future interface-admission guards planned in WP05-T012.
struct InterfaceCatalogEntry {
  InterfaceCandidate candidate;
  std::string_view name;
  InterfaceOwnerModule owner_module;
  InterfacePrimaryConsumer primary_consumer;
  InterfaceAdmissionReadiness readiness;
  std::string_view stable_anchor;
  std::string_view rationale;
};

// kInterfaceCatalog keeps only the candidate set that is both cross-module and
// traceable to current architecture artifacts. Readiness remains split so T012
// can review mature candidates without losing visibility into the rest.
inline constexpr std::array<InterfaceCatalogEntry, 10> kInterfaceCatalog = {{
    {
        InterfaceCandidate::IPlanner,
        "IPlanner",
        InterfaceOwnerModule::Cognition,
        InterfacePrimaryConsumer::Runtime,
        InterfaceAdmissionReadiness::AwaitingSupportingContracts,
        "GoalContract/ContextPacket/Observation",
        "runtime consumes cognition planning through an abstraction boundary, but PlanGraph and ReplanResult are not frozen yet",
    },
    {
        InterfaceCandidate::IToolManager,
        "IToolManager",
        InterfaceOwnerModule::Tools,
        InterfacePrimaryConsumer::Runtime,
        InterfaceAdmissionReadiness::ReviewReady,
        "ToolRequest/ToolResult/ToolDescriptor",
        "tool governance already has frozen request, result, and descriptor contracts",
    },
    {
        InterfaceCandidate::ILLMAdapter,
        "ILLMAdapter",
        InterfaceOwnerModule::Llm,
        InterfacePrimaryConsumer::Runtime,
        InterfaceAdmissionReadiness::ReviewReady,
        "LLMRequest/LLMResponse",
        "llm adapter already has a provider-neutral request/response contract handoff",
    },
    {
        InterfaceCandidate::IMemoryStore,
        "IMemoryStore",
        InterfaceOwnerModule::Memory,
        InterfacePrimaryConsumer::Runtime,
        InterfaceAdmissionReadiness::AwaitingSupportingContracts,
        "Turn/Session/SummaryMemory/MemoryFact",
        "memory objects are freezing, but session snapshot and retrieval helper contracts are not complete yet",
    },
    {
        InterfaceCandidate::IContextOrchestrator,
        "IContextOrchestrator",
        InterfaceOwnerModule::Memory,
        InterfacePrimaryConsumer::Runtime,
        InterfaceAdmissionReadiness::AwaitingSupportingContracts,
        "ContextPacket + ADR-006 responsibility chain",
        "context orchestration is a stable cross-module governance surface, but its request/result helper contracts are still implicit",
    },
    {
        InterfaceCandidate::IKnowledgeService,
        "IKnowledgeService",
        InterfaceOwnerModule::Knowledge,
        InterfacePrimaryConsumer::Runtime,
        InterfaceAdmissionReadiness::AwaitingSupportingContracts,
        "knowledge retrieval result semantics",
        "knowledge access is cross-module, but retrieval request/result contracts remain unfinished",
    },
    {
        InterfaceCandidate::IExecutionService,
        "IExecutionService",
        InterfaceOwnerModule::Services,
        InterfacePrimaryConsumer::Tools,
        InterfaceAdmissionReadiness::ReviewReady,
        "ServiceTypes Execution request/result objects + IExecutionService",
        "tools now consume a frozen execution facade with stable supporting objects and full smoke/failure/profile evidence",
    },
    {
        InterfaceCandidate::IDataService,
        "IDataService",
        InterfaceOwnerModule::Services,
        InterfacePrimaryConsumer::Tools,
        InterfaceAdmissionReadiness::ReviewReady,
        "ServiceTypes Data request/result objects + IDataService",
        "tools now consume a frozen query-only data facade with stable route/cache/profile evidence",
    },
    {
        InterfaceCandidate::IAgentRegistry,
        "IAgentRegistry",
        InterfaceOwnerModule::MultiAgent,
        InterfacePrimaryConsumer::Runtime,
        InterfaceAdmissionReadiness::AwaitingSupportingContracts,
        "ADR-008 worker matching semantics",
        "runtime depends on a registry abstraction for worker matching under multi-agent control",
    },
    {
        InterfaceCandidate::IResultMerger,
        "IResultMerger",
        InterfaceOwnerModule::MultiAgent,
        InterfacePrimaryConsumer::Runtime,
        InterfaceAdmissionReadiness::AwaitingSupportingContracts,
        "ADR-008 collaboration merge semantics",
        "runtime needs a stable merge abstraction, but collaboration result helper objects are still evolving",
    },
}};

// Returns the canonical interface name captured from the design evidence.
constexpr std::string_view interface_candidate_name(
    InterfaceCandidate candidate) {
  for (const auto& entry : kInterfaceCatalog) {
    if (entry.candidate == candidate) {
      return entry.name;
    }
  }

  return "UnknownInterface";
}

// Returns the top-level owner module name for reporting and future guard use.
constexpr std::string_view interface_owner_module_name(
    InterfaceOwnerModule owner_module) {
  switch (owner_module) {
    case InterfaceOwnerModule::Cognition:
      return "cognition";
    case InterfaceOwnerModule::Tools:
      return "tools";
    case InterfaceOwnerModule::Llm:
      return "llm";
    case InterfaceOwnerModule::Memory:
      return "memory";
    case InterfaceOwnerModule::Knowledge:
      return "knowledge";
    case InterfaceOwnerModule::Services:
      return "services";
    case InterfaceOwnerModule::MultiAgent:
      return "multi_agent";
  }

  return "unknown";
}

// Returns the primary consumer module name for a candidate interface.
constexpr std::string_view interface_primary_consumer_name(
    InterfacePrimaryConsumer primary_consumer) {
  switch (primary_consumer) {
    case InterfacePrimaryConsumer::Runtime:
      return "runtime";
    case InterfacePrimaryConsumer::Tools:
      return "tools";
  }

  return "unknown";
}

// Returns a human-readable readiness label used by tests and future guards.
constexpr std::string_view interface_admission_readiness_name(
    InterfaceAdmissionReadiness readiness) {
  switch (readiness) {
    case InterfaceAdmissionReadiness::ReviewReady:
      return "review_ready";
    case InterfaceAdmissionReadiness::AwaitingSupportingContracts:
      return "awaiting_supporting_contracts";
  }

  return "unknown";
}

// Returns the full catalog row for a candidate interface.
constexpr const InterfaceCatalogEntry* find_interface_catalog_entry(
    InterfaceCandidate candidate) {
  for (const auto& entry : kInterfaceCatalog) {
    if (entry.candidate == candidate) {
      return &entry;
    }
  }

  return nullptr;
}

// Finds a catalog row by canonical name. Returning nullptr makes absence
// assertions easy in contract tests without inventing a synthetic enum value.
constexpr const InterfaceCatalogEntry* find_interface_catalog_entry_by_name(
    std::string_view name) {
  for (const auto& entry : kInterfaceCatalog) {
    if (entry.name == name) {
      return &entry;
    }
  }

  return nullptr;
}

// Counts candidates by readiness so T012 can quickly reason about which rows
// are mature enough for admission review.
constexpr std::size_t count_interface_candidates_by_readiness(
    InterfaceAdmissionReadiness readiness) {
  std::size_t count = 0;
  for (const auto& entry : kInterfaceCatalog) {
    if (entry.readiness == readiness) {
      ++count;
    }
  }

  return count;
}

// Counts candidates owned by a given top-level module.
constexpr std::size_t count_interface_candidates_by_owner_module(
    InterfaceOwnerModule owner_module) {
  std::size_t count = 0;
  for (const auto& entry : kInterfaceCatalog) {
    if (entry.owner_module == owner_module) {
      ++count;
    }
  }

  return count;
}

// A convenience predicate for the two candidates that already have enough
// frozen supporting contracts to enter admission review immediately.
constexpr bool is_review_ready_interface_candidate(
    InterfaceCandidate candidate) {
  const auto* entry = find_interface_catalog_entry(candidate);
  return entry != nullptr &&
         entry->readiness == InterfaceAdmissionReadiness::ReviewReady;
}

}  // namespace dasall::contracts
