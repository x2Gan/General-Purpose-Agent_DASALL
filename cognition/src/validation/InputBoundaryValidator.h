#pragma once

#include <optional>
#include <string>
#include <vector>

#include "CognitionTypes.h"

namespace dasall::cognition::validation {

struct InputBoundaryValidationResult {
  std::vector<std::string> missing_fields;
  std::optional<contracts::ErrorInfo> error_info;

  [[nodiscard]] bool ok() const {
    return !error_info.has_value();
  }
};

class InputBoundaryValidator {
 public:
  [[nodiscard]] static InputBoundaryValidationResult validate_decide_request(
      const CognitionStepRequest& request);

  [[nodiscard]] static InputBoundaryValidationResult validate_reflection_request(
      const ReflectionRequest& request);

  [[nodiscard]] static InputBoundaryValidationResult validate_response_request(
      const ResponseBuildRequest& request);
};

}  // namespace dasall::cognition::validation