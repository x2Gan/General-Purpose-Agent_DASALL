#pragma once

#include <optional>
#include <string>

#include "error/ResultCode.h"

namespace dasall::contracts {

// ErrorDetails carries the minimal machine-readable diagnostics payload defined
// by WP02-T005. The details object is required and must include code/message/
// stage when validated by ErrorInfoGuards.
struct ErrorDetails {
  std::optional<int> code;
  std::string message;
  std::string stage;
};

// ErrorSourceRefMinimal is intentionally a minimal reference form in B004.
// Full source-ref lifecycle and relationship validation are addressed in B005.
struct ErrorSourceRefMinimal {
  std::string ref_type;
  std::string ref_id;
};

// ErrorInfo is the frozen cross-cutting error contract object seed for WP-02.
// Required top-level fields are:
// - failure_type
// - retryable
// - safe_to_replan
// - details
// - source_ref
// All required-ness is enforced through ErrorInfoGuards to keep this structure
// plain and easy to serialize.
struct ErrorInfo {
  std::optional<ResultCodeCategory> failure_type;
  std::optional<bool> retryable;
  std::optional<bool> safe_to_replan;
  ErrorDetails details;
  ErrorSourceRefMinimal source_ref;
};

}  // namespace dasall::contracts
