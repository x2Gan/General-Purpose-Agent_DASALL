#pragma once

#include "policy/PolicyTypes.h"

namespace dasall::infra::policy {

class PolicyDecisionProjector final {
 public:
  [[nodiscard]] PolicyDecisionRef project(const PolicyQueryContext& query,
                                          const PolicySnapshot& snapshot) const;
};

}  // namespace dasall::infra::policy