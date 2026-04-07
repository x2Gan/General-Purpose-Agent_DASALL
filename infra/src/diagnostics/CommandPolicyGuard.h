#pragma once

#include <string>

#include "diagnostics/IDiagnosticsPolicyGuard.h"

namespace dasall::infra::policy {
class ISecurityPolicyManager;
struct PolicyDecisionRef;
struct PolicyQueryContext;
}  // namespace dasall::infra::policy

namespace dasall::infra::diagnostics {

class CommandPolicyGuard final : public IDiagnosticsPolicyGuard {
 public:
  explicit CommandPolicyGuard(const policy::ISecurityPolicyManager& policy_manager);

  [[nodiscard]] CommandDecision authorize(const DiagnosticsCommand& command,
                                          const InfraContext& context) override;

 private:
  [[nodiscard]] static policy::PolicyQueryContext build_query(const DiagnosticsCommand& command,
                                                              const InfraContext& context);
  [[nodiscard]] static std::string build_policy_ref(const policy::PolicyDecisionRef& decision);

  const policy::ISecurityPolicyManager& policy_manager_;
};

}  // namespace dasall::infra::diagnostics