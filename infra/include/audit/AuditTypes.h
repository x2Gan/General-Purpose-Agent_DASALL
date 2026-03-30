#pragma once

#include <algorithm>
#include <cstdint>
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
  WorkerTask = 3,
};

struct AuditEvidenceRef {
  AuditEvidenceKind kind = AuditEvidenceKind::Unspecified;
  std::string ref;

  [[nodiscard]] bool has_value() const {
    return kind != AuditEvidenceKind::Unspecified && !ref.empty();
  }

  [[nodiscard]] bool references_contract_anchor() const {
    return kind == AuditEvidenceKind::ToolResult ||
           kind == AuditEvidenceKind::RecoveryOutcome ||
           kind == AuditEvidenceKind::WorkerTask;
  }

  [[nodiscard]] bool is_contract_outcome_ref() const {
    return references_contract_anchor();
  }
};

struct AuditEvent {
  using SideEffects = std::vector<std::string>;

  std::string event_id;
  std::string action;
  std::string actor;
  std::string target;
  AuditOutcome outcome = AuditOutcome::Unspecified;
  AuditEvidenceRef evidence_ref;
  SideEffects side_effects;
  std::int64_t timestamp = 0;

  [[nodiscard]] bool has_required_fields() const {
    return !event_id.empty() && !action.empty() && !actor.empty() &&
           !target.empty() && outcome != AuditOutcome::Unspecified &&
           evidence_ref.has_value() && timestamp > 0;
  }

  [[nodiscard]] bool references_contract_boundary() const {
    return evidence_ref.has_value() && evidence_ref.references_contract_anchor();
  }

  [[nodiscard]] bool references_contract_outcome() const {
    return references_contract_boundary();
  }

  [[nodiscard]] bool side_effects_are_serializable() const {
    for (std::size_t index = 0; index < side_effects.size(); ++index) {
      if (side_effects[index].empty()) {
        return false;
      }

      if (std::find(side_effects.begin() + static_cast<std::ptrdiff_t>(index + 1),
                    side_effects.end(),
                    side_effects[index]) != side_effects.end()) {
        return false;
      }
    }

    return true;
  }
};

}  // namespace dasall::infra