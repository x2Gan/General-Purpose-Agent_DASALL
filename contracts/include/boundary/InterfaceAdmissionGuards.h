#pragma once

#include <cstddef>
#include <string_view>

#include "boundary/InterfaceCatalog.h"

namespace dasall::contracts {

// InterfaceAdmissionDecision compresses T012 admission review into three
// stable outcomes: admit into shared contracts now, postpone until supporting
// contracts freeze, or return to module scope because the interface is not a
// valid shared-contract candidate.
enum class InterfaceAdmissionDecision {
  Admit,
  Postpone,
  Return,
};

// InterfaceAdmissionResult carries the binary admission outcome together with a
// normalized decision and reason string, allowing contract tests and future CI
// gates to assert behavior without duplicating rule-to-message mapping logic.
struct InterfaceAdmissionResult {
  bool admitted = false;
  InterfaceAdmissionDecision decision = InterfaceAdmissionDecision::Return;
  std::string_view reason =
      "interface candidate must return to module scope because it is not catalogued";
};

// Returns true when a catalog row has the minimum metadata required for an
// admission review: canonical name, stable anchor, and rationale. This avoids
// admitting partially described rows that cannot be traced during review.
constexpr bool has_complete_interface_admission_metadata(
    const InterfaceCatalogEntry& entry) {
  return !entry.name.empty() && !entry.stable_anchor.empty() &&
         !entry.rationale.empty();
}

// Returns true when owner and primary consumer represent an actual cross-module
// dependency boundary rather than an internal same-module abstraction.
constexpr bool is_cross_module_interface_dependency(
    const InterfaceCatalogEntry& entry) {
  return interface_owner_module_name(entry.owner_module) !=
         interface_primary_consumer_name(entry.primary_consumer);
}

// Evaluates admission for one catalog row. The rule order is deliberate:
// malformed metadata and same-module boundaries return first, then readiness
// decides whether the candidate is admitted now or postponed for later rounds.
constexpr InterfaceAdmissionResult evaluate_interface_admission_entry(
    const InterfaceCatalogEntry& entry) {
  if (!has_complete_interface_admission_metadata(entry)) {
    return InterfaceAdmissionResult{
        .admitted = false,
        .decision = InterfaceAdmissionDecision::Return,
        .reason =
            "interface candidate must return to module scope because catalog metadata is incomplete",
    };
  }

  if (!is_cross_module_interface_dependency(entry)) {
    return InterfaceAdmissionResult{
        .admitted = false,
        .decision = InterfaceAdmissionDecision::Return,
        .reason =
            "interface candidate must return to module scope because owner and primary consumer are not cross-module",
    };
  }

  switch (entry.readiness) {
    case InterfaceAdmissionReadiness::ReviewReady:
      return InterfaceAdmissionResult{
          .admitted = true,
          .decision = InterfaceAdmissionDecision::Admit,
          .reason =
              "interface candidate is admitted into shared contracts",
      };

    case InterfaceAdmissionReadiness::AwaitingSupportingContracts:
      return InterfaceAdmissionResult{
          .admitted = false,
          .decision = InterfaceAdmissionDecision::Postpone,
          .reason =
              "interface candidate is postponed until supporting contracts freeze",
      };
  }

  return InterfaceAdmissionResult{};
}

// Evaluates admission for a candidate already known to the catalog.
constexpr InterfaceAdmissionResult evaluate_interface_admission(
    InterfaceCandidate candidate) {
  const auto* entry = find_interface_catalog_entry(candidate);
  if (entry == nullptr) {
    return InterfaceAdmissionResult{};
  }

  return evaluate_interface_admission_entry(*entry);
}

// Evaluates admission by interface name. Non-catalogued interfaces are
// normalized to a Return decision so callers can gate ad hoc suggestions.
constexpr InterfaceAdmissionResult evaluate_interface_admission_by_name(
    std::string_view name) {
  const auto* entry = find_interface_catalog_entry_by_name(name);
  if (entry == nullptr) {
    return InterfaceAdmissionResult{
        .admitted = false,
        .decision = InterfaceAdmissionDecision::Return,
        .reason =
            "interface candidate must return to module scope because it is not catalogued",
    };
  }

  return evaluate_interface_admission_entry(*entry);
}

// Counts how many current catalog candidates are admitted now. T012 uses this
// to lock the initial admission baseline and to detect accidental drift.
constexpr std::size_t count_admitted_interface_candidates() {
  std::size_t count = 0;
  for (const auto& entry : kInterfaceCatalog) {
    if (evaluate_interface_admission_entry(entry).admitted) {
      ++count;
    }
  }

  return count;
}

// Convenience predicate for callers that only need the binary admission state.
constexpr bool can_admit_interface_candidate(InterfaceCandidate candidate) {
  return evaluate_interface_admission(candidate).admitted;
}

}  // namespace dasall::contracts
