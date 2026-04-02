#pragma once

#include "config/IConfigValidator.h"

namespace dasall::infra::config {

class ConfigValidator final : public IConfigValidator {
 public:
  [[nodiscard]] ConfigValidationResult validate(const ConfigSnapshot& snapshot) const override;
  [[nodiscard]] ConfigValidationResult validate_patch(const ConfigSnapshot& current_snapshot,
                                                      const ConfigPatch& patch) const override;
};

}  // namespace dasall::infra::config