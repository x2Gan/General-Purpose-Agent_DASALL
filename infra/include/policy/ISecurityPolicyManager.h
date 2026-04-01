#pragma once

#include <string>

#include "PolicyTypes.h"

namespace dasall::infra::policy {

class ISecurityPolicyManager {
 public:
  virtual ~ISecurityPolicyManager() = default;

  virtual PolicyOpResult load_policy(const PolicyBundle& bundle) = 0;
  virtual PolicyOpResult apply_patch(const PolicyPatch& patch) = 0;
  virtual ValidationReport dry_run_patch(const PolicyPatch& patch) = 0;
  [[nodiscard]] virtual PolicySnapshot snapshot() const = 0;
  virtual PolicyOpResult rollback(const std::string& snapshot_id) = 0;
  [[nodiscard]] virtual PolicyDecisionRef evaluate(const PolicyQueryContext& query) const = 0;
};

}  // namespace dasall::infra::policy