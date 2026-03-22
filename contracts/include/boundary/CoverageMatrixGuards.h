#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace dasall::contracts {

// CoverageRiskObject identifies the high-risk objects that must remain
// continuously protected by at least one dedicated contract test.
enum class CoverageRiskObject {
  AgentRequest,
  EventEnvelope,
  ResultCode,
  EnumLifecycle,
  ContextPacket,
  ReflectionDecision,
  MultiAgentResult,
};

// CoverageContractTestKind normalizes the stable test entrypoints produced by
// WP05-T013-B to WP05-T016-B so matrix guards can evaluate them uniformly.
enum class CoverageContractTestKind {
  SerializationCompatibility,
  ErrorCodeEnumCompatibility,
  EventEnvelopeCompatibility,
  ADRBoundaryRegression,
};

// CoverageMatrixCatalogEntry binds one high-risk object to one stable contract
// test entrypoint and records why this mapping exists.
struct CoverageMatrixCatalogEntry {
  CoverageRiskObject object;
  std::string_view object_name;
  CoverageContractTestKind test_kind;
  std::string_view test_name;
  std::string_view rationale;
};

// CoverageMatrixExecutionSnapshot represents which matrix-level contract tests
// have been executed and passed in the current validation round.
struct CoverageMatrixExecutionSnapshot {
  bool serialization_compatibility_passed = false;
  bool error_code_enum_compatibility_passed = false;
  bool event_envelope_compatibility_passed = false;
  bool adr_boundary_regression_passed = false;
};

// CoverageMatrixValidationResult returns a fully traceable decision for
// checklist/gate usage, including the first uncovered object for fast triage.
struct CoverageMatrixValidationResult {
  bool ok = false;
  bool catalog_complete = false;
  bool all_risk_objects_covered = false;
  std::size_t uncovered_risk_object_count = 0;
  std::string_view first_uncovered_risk_object = "none";
  std::string_view first_failed_check = "not-run";
  std::string_view reason = "coverage matrix validation not yet run";
};

inline constexpr std::size_t kCoverageRiskObjectCount = 7;
inline constexpr std::size_t kCoverageMatrixCatalogEntryCount = 8;

inline constexpr std::array<CoverageMatrixCatalogEntry,
                            kCoverageMatrixCatalogEntryCount>
    kCoverageMatrixCatalog = {{
        {
            CoverageRiskObject::AgentRequest,
            "AgentRequest",
            CoverageContractTestKind::SerializationCompatibility,
            "SerializationCompatibilityContractTest",
            "AgentRequest entry fields must keep serialization compatibility",
        },
        {
            CoverageRiskObject::EventEnvelope,
            "EventEnvelope",
            CoverageContractTestKind::SerializationCompatibility,
            "SerializationCompatibilityContractTest",
            "EventEnvelope base serialization must remain stable",
        },
        {
            CoverageRiskObject::EventEnvelope,
            "EventEnvelope",
            CoverageContractTestKind::EventEnvelopeCompatibility,
            "EventEnvelopeCompatibilityContractTest",
            "EventEnvelope header/payload compatibility must be continuously checked",
        },
        {
            CoverageRiskObject::ResultCode,
            "ResultCode",
            CoverageContractTestKind::ErrorCodeEnumCompatibility,
            "ErrorCodeEnumCompatibilityContractTest",
            "ResultCode numeric/category freeze requires compatibility regression",
        },
        {
            CoverageRiskObject::EnumLifecycle,
            "EnumLifecycle",
            CoverageContractTestKind::ErrorCodeEnumCompatibility,
            "ErrorCodeEnumCompatibilityContractTest",
            "Enum lifecycle compatibility must preserve unknown/deprecated handling",
        },
        {
            CoverageRiskObject::ContextPacket,
            "ContextPacket",
            CoverageContractTestKind::ADRBoundaryRegression,
            "ADRBoundaryRegressionContractTest",
            "ADR-006 boundary fields must remain guarded",
        },
        {
            CoverageRiskObject::ReflectionDecision,
            "ReflectionDecision",
            CoverageContractTestKind::ADRBoundaryRegression,
            "ADRBoundaryRegressionContractTest",
            "ADR-007 boundary fields must remain guarded",
        },
        {
            CoverageRiskObject::MultiAgentResult,
            "MultiAgentResult",
            CoverageContractTestKind::ADRBoundaryRegression,
            "ADRBoundaryRegressionContractTest",
            "ADR-008 boundary fields must remain guarded",
        },
    }};

inline constexpr std::array<CoverageRiskObject, kCoverageRiskObjectCount>
    kCoverageRiskObjects = {
        CoverageRiskObject::AgentRequest,
        CoverageRiskObject::EventEnvelope,
        CoverageRiskObject::ResultCode,
        CoverageRiskObject::EnumLifecycle,
        CoverageRiskObject::ContextPacket,
        CoverageRiskObject::ReflectionDecision,
        CoverageRiskObject::MultiAgentResult,
};

      // Returns a stable display name for diagnostic output and TODO evidence.
constexpr std::string_view coverage_risk_object_name(CoverageRiskObject object) {
  switch (object) {
    case CoverageRiskObject::AgentRequest:
      return "AgentRequest";
    case CoverageRiskObject::EventEnvelope:
      return "EventEnvelope";
    case CoverageRiskObject::ResultCode:
      return "ResultCode";
    case CoverageRiskObject::EnumLifecycle:
      return "EnumLifecycle";
    case CoverageRiskObject::ContextPacket:
      return "ContextPacket";
    case CoverageRiskObject::ReflectionDecision:
      return "ReflectionDecision";
    case CoverageRiskObject::MultiAgentResult:
      return "MultiAgentResult";
  }

  return "UnknownCoverageRiskObject";
}

// Maps one normalized test kind to its pass/fail bit in the execution
// snapshot, allowing the catalog loop to evaluate coverage generically.
constexpr bool is_coverage_test_passed(
    const CoverageMatrixExecutionSnapshot& snapshot,
    CoverageContractTestKind test_kind) {
  switch (test_kind) {
    case CoverageContractTestKind::SerializationCompatibility:
      return snapshot.serialization_compatibility_passed;
    case CoverageContractTestKind::ErrorCodeEnumCompatibility:
      return snapshot.error_code_enum_compatibility_passed;
    case CoverageContractTestKind::EventEnvelopeCompatibility:
      return snapshot.event_envelope_compatibility_passed;
    case CoverageContractTestKind::ADRBoundaryRegression:
      return snapshot.adr_boundary_regression_passed;
  }

  return false;
}

// Counts how many catalog rows currently point to the given high-risk object.
constexpr std::size_t count_catalog_mappings_for_risk_object(
    CoverageRiskObject object) {
  std::size_t count = 0;
  for (const auto& entry : kCoverageMatrixCatalog) {
    if (entry.object == object) {
      ++count;
    }
  }

  return count;
}

// Fast predicate used by catalog integrity checks.
constexpr bool has_catalog_mapping_for_risk_object(CoverageRiskObject object) {
  return count_catalog_mappings_for_risk_object(object) > 0;
}

// Returns true when at least one mapped contract test has passed for the
// specified high-risk object in the current validation round.
constexpr bool is_risk_object_covered(
    const CoverageMatrixExecutionSnapshot& snapshot,
    CoverageRiskObject object) {
  for (const auto& entry : kCoverageMatrixCatalog) {
    if (entry.object == object && is_coverage_test_passed(snapshot, entry.test_kind)) {
      return true;
    }
  }

  return false;
}

// Counts uncovered high-risk objects after applying the current execution
// snapshot to the catalog mapping rules.
constexpr std::size_t count_uncovered_risk_objects(
    const CoverageMatrixExecutionSnapshot& snapshot) {
  std::size_t count = 0;
  for (const auto object : kCoverageRiskObjects) {
    if (!is_risk_object_covered(snapshot, object)) {
      ++count;
    }
  }

  return count;
}

// Returns the first uncovered object name in catalog order so failure output
// remains deterministic across environments.
constexpr std::string_view first_uncovered_risk_object_name(
    const CoverageMatrixExecutionSnapshot& snapshot) {
  for (const auto object : kCoverageRiskObjects) {
    if (!is_risk_object_covered(snapshot, object)) {
      return coverage_risk_object_name(object);
    }
  }

  return "none";
}

// Verifies the catalog integrity rule: each high-risk object must have at
// least one contract-test mapping.
constexpr bool has_minimum_contract_test_for_every_risk_object() {
  for (const auto object : kCoverageRiskObjects) {
    if (!has_catalog_mapping_for_risk_object(object)) {
      return false;
    }
  }

  return true;
}

// Runs full coverage-matrix validation and returns the first failing check,
// supporting automated gate decisions and readable diagnostics.
inline constexpr CoverageMatrixValidationResult validate_coverage_matrix(
    const CoverageMatrixExecutionSnapshot& snapshot) {
  if (!has_minimum_contract_test_for_every_risk_object()) {
    return CoverageMatrixValidationResult{
        .ok = false,
        .catalog_complete = false,
        .all_risk_objects_covered = false,
        .uncovered_risk_object_count = kCoverageRiskObjectCount,
        .first_uncovered_risk_object = "catalog",
        .first_failed_check = "catalog-completeness",
        .reason = "coverage matrix catalog is incomplete for high-risk objects",
    };
  }

  const auto uncovered_count = count_uncovered_risk_objects(snapshot);
  if (uncovered_count > 0) {
    return CoverageMatrixValidationResult{
        .ok = false,
        .catalog_complete = true,
        .all_risk_objects_covered = false,
        .uncovered_risk_object_count = uncovered_count,
        .first_uncovered_risk_object = first_uncovered_risk_object_name(snapshot),
        .first_failed_check = "risk-object-coverage",
        .reason = "coverage matrix has uncovered high-risk objects",
    };
  }

  return CoverageMatrixValidationResult{
      .ok = true,
      .catalog_complete = true,
      .all_risk_objects_covered = true,
      .uncovered_risk_object_count = 0,
      .first_uncovered_risk_object = "none",
      .first_failed_check = "none",
      .reason = "coverage matrix validation passed",
  };
}

}  // namespace dasall::contracts