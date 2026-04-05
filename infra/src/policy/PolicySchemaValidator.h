#pragma once

#include "policy/IPolicySchemaValidator.h"

namespace dasall::infra::policy {

class PolicySchemaValidator final : public IPolicySchemaValidator {
 public:
  [[nodiscard]] ValidationReport validate_bundle(const PolicyBundle& bundle) const override;
  [[nodiscard]] ValidationReport validate_patch(const PolicySnapshot& current_snapshot,
                                               const PolicyPatch& patch) const override;
};

}  // namespace dasall::infra::policy