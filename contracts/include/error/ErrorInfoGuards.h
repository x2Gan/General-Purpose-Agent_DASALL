#pragma once

#include <string_view>

#include "boundary/GuardCommon.h"
#include "error/ErrorInfo.h"

namespace dasall::contracts {

struct ErrorInfoGuardResult {
  bool ok = false;
  std::string_view reason = "error info validation failed";
};

// Validates required fields and the minimal semantics promised by WP02-T005.
inline ErrorInfoGuardResult validate_error_info_required_fields(const ErrorInfo& error_info) {
  if (!error_info.failure_type.has_value()) {
    return ErrorInfoGuardResult{.ok = false, .reason = "failure_type is required"};
  }

  if (*error_info.failure_type == ResultCodeCategory::Unknown) {
    return ErrorInfoGuardResult{.ok = false, .reason = "failure_type cannot be unknown"};
  }

  if (!error_info.retryable.has_value()) {
    return ErrorInfoGuardResult{.ok = false, .reason = "retryable is required"};
  }

  if (!error_info.safe_to_replan.has_value()) {
    return ErrorInfoGuardResult{.ok = false, .reason = "safe_to_replan is required"};
  }

  if (!error_info.details.code.has_value()) {
    return ErrorInfoGuardResult{.ok = false, .reason = "details.code is required"};
  }

  if (error_info.details.message.empty()) {
    return ErrorInfoGuardResult{.ok = false, .reason = "details.message is required"};
  }

  if (error_info.details.stage.empty()) {
    return ErrorInfoGuardResult{.ok = false, .reason = "details.stage is required"};
  }

  if (error_info.source_ref.ref_type.empty()) {
    return ErrorInfoGuardResult{.ok = false, .reason = "source_ref.ref_type is required"};
  }

  if (!is_supported_error_source_ref_type(error_info.source_ref.ref_type)) {
    return ErrorInfoGuardResult{.ok = false, .reason = "source_ref.ref_type is not supported"};
  }

  if (error_info.source_ref.ref_id.empty()) {
    return ErrorInfoGuardResult{.ok = false, .reason = "source_ref.ref_id is required"};
  }

  return ErrorInfoGuardResult{.ok = true, .reason = "error info is valid"};
}

}  // namespace dasall::contracts
