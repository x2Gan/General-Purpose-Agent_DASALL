#pragma once

#include <string>

#include "error/ResultCode.h"

namespace dasall::infra::watchdog {

struct RecoveryHintRequest {
  contracts::ResultCode reason_code = static_cast<contracts::ResultCode>(0);
  std::string target_ref;
  std::string suggested_action;
  std::string evidence_ref;

  [[nodiscard]] bool references_contract_reason_code() const {
    return contracts::classify_result_code(reason_code) !=
           contracts::ResultCodeCategory::Unknown;
  }

  [[nodiscard]] bool has_required_fields() const {
    return references_contract_reason_code() && !target_ref.empty() &&
           !suggested_action.empty() && !evidence_ref.empty();
  }
};

}  // namespace dasall::infra::watchdog