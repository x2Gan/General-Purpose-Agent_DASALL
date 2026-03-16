#pragma once

#include <cstddef>
#include <string_view>

#include "boundary/GuardCommon.h"
#include "error/ErrorSourceRef.h"

namespace dasall::contracts {

struct ErrorSourceGuardResult {
  bool ok = false;
  std::string_view reason = "error source validation failed";
};

// Validates the T006 frozen requirements:
// 1) exactly one primary reference,
// 2) ref_type is in the four allowed kinds,
// 3) ref_id is non-empty.
inline ErrorSourceGuardResult validate_error_source_refs(const ErrorSourceRefSet& source_refs) {
  if (source_refs.refs.empty()) {
    return ErrorSourceGuardResult{.ok = false, .reason = "at least one source ref is required"};
  }

  std::size_t primary_count = 0;
  for (const auto& ref : source_refs.refs) {
    if (ref.primary) {
      ++primary_count;
    }

    if (!is_supported_error_source_ref_type(ref.ref_type)) {
      return ErrorSourceGuardResult{.ok = false, .reason = "source ref type is not supported"};
    }

    if (ref.ref_id.empty()) {
      return ErrorSourceGuardResult{.ok = false, .reason = "source ref id is required"};
    }
  }

  if (primary_count != 1U) {
    return ErrorSourceGuardResult{.ok = false, .reason = "exactly one primary source ref is required"};
  }

  return ErrorSourceGuardResult{.ok = true, .reason = "error source refs are valid"};
}

}  // namespace dasall::contracts
