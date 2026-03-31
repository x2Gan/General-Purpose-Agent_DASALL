#pragma once

#include <string>

#include "error/ResultCode.h"

namespace dasall::infra {

enum class RecoveryHintSeverity {
  Unspecified = 0,
  Warning = 1,
  Critical = 2,
  Fatal = 3,
};

struct RecoveryHint {
  contracts::ResultCode reason_code = static_cast<contracts::ResultCode>(0);
  RecoveryHintSeverity severity = RecoveryHintSeverity::Unspecified;
  std::string suggested_action;
  std::string evidence_ref;

  [[nodiscard]] bool references_contract_reason_code() const {
    return contracts::classify_result_code(reason_code) !=
           contracts::ResultCodeCategory::Unknown;
  }

  [[nodiscard]] bool has_required_fields() const {
    return references_contract_reason_code() &&
           severity != RecoveryHintSeverity::Unspecified &&
           !suggested_action.empty() && !evidence_ref.empty();
  }
};

}  // namespace dasall::infra