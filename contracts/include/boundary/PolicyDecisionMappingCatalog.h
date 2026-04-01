#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace dasall::contracts {

// SharedPolicyDecisionSemantic keeps the only three decision semantics that
// infra/policy is currently allowed to align with while contracts::policy
// still lacks a concrete PolicyDecision object under T010.
enum class SharedPolicyDecisionSemantic {
  Allow,
  Deny,
  RequireConfirmation,
};

// PolicyDecisionSemanticMappingEntry records the semantic replacement that is
// currently approved instead of a direct contracts::policy::PolicyDecision
// object dependency.
struct PolicyDecisionSemanticMappingEntry {
  SharedPolicyDecisionSemantic semantic;
  std::string_view contracts_object_name;
  std::string_view replacement_carrier;
  std::string_view semantic_name;
  std::string_view rationale;
};

// PolicyDecisionRefPrivateFieldEntry captures the trace-only fields that remain
// infra-private on PolicyDecisionRef and must not be treated as evidence that a
// shared PolicyDecision object already exists.
struct PolicyDecisionRefPrivateFieldEntry {
  std::string_view field_name;
  std::string_view rationale;
};

struct PolicyDecisionMappingValidationResult {
  bool ok = false;
  bool semantic_catalog_complete = false;
  bool private_field_catalog_complete = false;
  bool no_duplicate_entries = false;
  std::string_view first_failed_check = "not-run";
  std::string_view reason = "policy decision mapping validation not yet run";
};

inline constexpr std::array<PolicyDecisionSemanticMappingEntry, 3>
    kPolicyDecisionSemanticMappingCatalog = {{
        {
            SharedPolicyDecisionSemantic::Allow,
            "PolicyDecision",
            "infra::policy::PolicyDecisionRef.decision",
            "allow",
            "shared PolicyDecision object is absent, so infra aligns only the allow semantic through PolicyDecisionRef",
        },
        {
            SharedPolicyDecisionSemantic::Deny,
            "PolicyDecision",
            "infra::policy::PolicyDecisionRef.decision",
            "deny",
            "shared PolicyDecision object is absent, so infra aligns only the deny semantic through PolicyDecisionRef",
        },
        {
            SharedPolicyDecisionSemantic::RequireConfirmation,
            "PolicyDecision",
            "infra::policy::PolicyDecisionRef.decision",
            "require_confirmation",
            "shared PolicyDecision object is absent, so infra aligns only the require_confirmation semantic through PolicyDecisionRef",
        },
    }};

inline constexpr std::array<PolicyDecisionRefPrivateFieldEntry, 6>
    kPolicyDecisionRefPrivateFieldCatalog = {{
        {
            "reason_code",
            "reason_code remains an infra-private trace field until contracts freezes a shared policy decision payload",
        },
        {
            "matched_rule_ids",
            "matched rule identifiers are infra-private explainability data and must not be treated as shared contract fields",
        },
        {
            "snapshot_id",
            "snapshot_id is a policy snapshot reference and not proof of a shared PolicyDecision object",
        },
        {
            "generation",
            "generation is a policy snapshot reference and not proof of a shared PolicyDecision object",
        },
        {
            "evidence_ref",
            "evidence_ref remains an infra-private audit trace anchor",
        },
        {
            "warnings",
            "warnings are infra-private advisory data and must not leak into the absent shared object surface",
        },
    }};

constexpr const PolicyDecisionSemanticMappingEntry*
find_policy_decision_semantic_mapping(SharedPolicyDecisionSemantic semantic) {
  for (const auto& entry : kPolicyDecisionSemanticMappingCatalog) {
    if (entry.semantic == semantic) {
      return &entry;
    }
  }

  return nullptr;
}

constexpr std::string_view shared_policy_decision_semantic_name(
    SharedPolicyDecisionSemantic semantic) {
  const auto* entry = find_policy_decision_semantic_mapping(semantic);
  return entry != nullptr ? entry->semantic_name : "unknown";
}

constexpr bool is_infra_private_policy_decision_ref_field(
    std::string_view field_name) {
  for (const auto& entry : kPolicyDecisionRefPrivateFieldCatalog) {
    if (entry.field_name == field_name) {
      return true;
    }
  }

  return false;
}

constexpr bool has_duplicate_policy_decision_semantics() {
  for (std::size_t left = 0; left < kPolicyDecisionSemanticMappingCatalog.size(); ++left) {
    for (std::size_t right = left + 1; right < kPolicyDecisionSemanticMappingCatalog.size();
         ++right) {
      if (kPolicyDecisionSemanticMappingCatalog[left].semantic ==
          kPolicyDecisionSemanticMappingCatalog[right].semantic) {
        return true;
      }
    }
  }

  return false;
}

constexpr bool has_duplicate_policy_decision_ref_private_fields() {
  for (std::size_t left = 0; left < kPolicyDecisionRefPrivateFieldCatalog.size(); ++left) {
    for (std::size_t right = left + 1; right < kPolicyDecisionRefPrivateFieldCatalog.size();
         ++right) {
      if (kPolicyDecisionRefPrivateFieldCatalog[left].field_name ==
          kPolicyDecisionRefPrivateFieldCatalog[right].field_name) {
        return true;
      }
    }
  }

  return false;
}

constexpr PolicyDecisionMappingValidationResult
validate_policy_decision_mapping_catalog() {
  const bool semantic_catalog_complete =
      kPolicyDecisionSemanticMappingCatalog.size() == 3 &&
      find_policy_decision_semantic_mapping(SharedPolicyDecisionSemantic::Allow) != nullptr &&
      find_policy_decision_semantic_mapping(SharedPolicyDecisionSemantic::Deny) != nullptr &&
      find_policy_decision_semantic_mapping(
          SharedPolicyDecisionSemantic::RequireConfirmation) != nullptr;

  if (!semantic_catalog_complete) {
    return PolicyDecisionMappingValidationResult{
        .ok = false,
        .semantic_catalog_complete = false,
        .private_field_catalog_complete = false,
        .no_duplicate_entries = false,
        .first_failed_check = "semantic_catalog_complete",
        .reason = "policy decision semantic catalog must cover allow/deny/require_confirmation only",
    };
  }

  const bool private_field_catalog_complete =
      kPolicyDecisionRefPrivateFieldCatalog.size() == 6 &&
      is_infra_private_policy_decision_ref_field("reason_code") &&
      is_infra_private_policy_decision_ref_field("matched_rule_ids") &&
      is_infra_private_policy_decision_ref_field("snapshot_id") &&
      is_infra_private_policy_decision_ref_field("generation") &&
      is_infra_private_policy_decision_ref_field("evidence_ref") &&
      is_infra_private_policy_decision_ref_field("warnings") &&
      !is_infra_private_policy_decision_ref_field("decision");

  if (!private_field_catalog_complete) {
    return PolicyDecisionMappingValidationResult{
        .ok = false,
        .semantic_catalog_complete = true,
        .private_field_catalog_complete = false,
        .no_duplicate_entries = false,
        .first_failed_check = "private_field_catalog_complete",
        .reason = "policy decision ref private-field catalog must freeze the six trace-only fields",
    };
  }

  const bool no_duplicate_entries = !has_duplicate_policy_decision_semantics() &&
                                    !has_duplicate_policy_decision_ref_private_fields();

  if (!no_duplicate_entries) {
    return PolicyDecisionMappingValidationResult{
        .ok = false,
        .semantic_catalog_complete = true,
        .private_field_catalog_complete = true,
        .no_duplicate_entries = false,
        .first_failed_check = "no_duplicate_entries",
        .reason = "policy decision mapping catalogs must not contain duplicate semantics or field names",
    };
  }

  return PolicyDecisionMappingValidationResult{
      .ok = true,
      .semantic_catalog_complete = true,
      .private_field_catalog_complete = true,
      .no_duplicate_entries = true,
      .first_failed_check = "none",
      .reason = "policy decision mapping catalog is valid",
  };
}

}  // namespace dasall::contracts