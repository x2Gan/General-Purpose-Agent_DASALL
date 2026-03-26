#pragma once

#include <algorithm>
#include <string>
#include <vector>

namespace dasall::infra {

enum class AuditOutcome {
  Unspecified = 0,
  Succeeded = 1,
  Failed = 2,
  Rejected = 3,
  Escalated = 4,
};

enum class AuditEvidenceKind {
  Unspecified = 0,
  ToolResult = 1,
  RecoveryOutcome = 2,
};

struct AuditEvidenceRef {
  AuditEvidenceKind kind = AuditEvidenceKind::Unspecified;
  std::string ref;

  [[nodiscard]] bool has_value() const {
    return kind != AuditEvidenceKind::Unspecified && !ref.empty();
  }

  [[nodiscard]] bool is_contract_outcome_ref() const {
    return kind == AuditEvidenceKind::ToolResult ||
           kind == AuditEvidenceKind::RecoveryOutcome;
  }
};

struct AuditEvent {
  using SideEffects = std::vector<std::string>;

  std::string action;
  std::string actor;
  std::string target;
  AuditEvidenceRef evidence_ref;
  AuditOutcome outcome = AuditOutcome::Unspecified;
  SideEffects side_effects;

  [[nodiscard]] bool has_required_fields() const {
    return !action.empty() && !actor.empty() && !target.empty() &&
           evidence_ref.has_value() && outcome != AuditOutcome::Unspecified;
  }

  [[nodiscard]] bool references_contract_outcome() const {
    return evidence_ref.has_value() && evidence_ref.is_contract_outcome_ref();
  }

  [[nodiscard]] bool side_effects_are_serializable() const {
    for (std::size_t index = 0; index < side_effects.size(); ++index) {
      if (side_effects[index].empty()) {
        return false;
      }

      if (std::find(side_effects.begin() + static_cast<std::ptrdiff_t>(index + 1),
                    side_effects.end(), side_effects[index]) != side_effects.end()) {
        return false;
      }
    }

    return true;
  }
};

}  // namespace dasall::infra