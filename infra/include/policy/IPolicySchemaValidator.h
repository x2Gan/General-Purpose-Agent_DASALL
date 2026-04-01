#pragma once

#include "PolicyTypes.h"

namespace dasall::infra::policy {

class IPolicySchemaValidator {
 public:
  virtual ~IPolicySchemaValidator() = default;

  [[nodiscard]] virtual ValidationReport validate_bundle(const PolicyBundle& bundle) const = 0;
  [[nodiscard]] virtual ValidationReport validate_patch(const PolicySnapshot& current_snapshot,
                                                        const PolicyPatch& patch) const = 0;
};

}  // namespace dasall::infra::policy