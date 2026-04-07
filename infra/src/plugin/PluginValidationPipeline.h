#pragma once

#include <functional>
#include <string>

#include "plugin/IPluginManager.h"
#include "plugin/IPluginPolicyGate.h"
#include "plugin/PluginErrorCode.h"
#include "policy/ISecurityPolicyManager.h"

namespace dasall::infra::plugin {

class PluginAuditAdapter;

struct PluginValidationStageResult {
  bool passed = false;
  std::string report_ref;
  std::string evidence_ref;
  PluginErrorCode error_code = PluginErrorCode::ValidateFail;
  std::string reason_code;
  std::string message;

  [[nodiscard]] static PluginValidationStageResult success(
      std::string report_ref,
      std::string evidence_ref,
      std::string reason_code = {});

  [[nodiscard]] static PluginValidationStageResult failure(
      PluginErrorCode error_code,
      std::string report_ref,
      std::string evidence_ref,
      std::string reason_code,
      std::string message);

  [[nodiscard]] bool is_valid() const;
};

using PluginValidationStageCallback =
    std::function<PluginValidationStageResult(const PluginValidationRequest&)>;

class PluginValidationPipeline {
 public:
  PluginValidationPipeline(
      const policy::ISecurityPolicyManager* security_policy_manager = nullptr,
      const IPluginPolicyGate* policy_gate = nullptr,
      PluginValidationStageCallback signature_stage = {},
      PluginValidationStageCallback compatibility_stage = {},
      PluginAuditAdapter* audit_adapter = nullptr,
      std::string audit_actor_ref = "runtime");

  [[nodiscard]] PluginValidationResult validate(
      const PluginValidationRequest& request) const;

 private:
  [[nodiscard]] PluginPolicyRequest make_policy_request(
      const PluginValidationRequest& request) const;
  [[nodiscard]] PluginValidationStageResult run_signature_stage(
      const PluginValidationRequest& request) const;
  [[nodiscard]] PluginValidationStageResult run_compatibility_stage(
      const PluginValidationRequest& request) const;
    void emit_policy_deny_audit(const PluginValidationRequest& request,
                                                            const policy::PolicyDecisionRef& policy_decision) const;
    void emit_stage_failure_audit(const PluginValidationRequest& request,
                                                                const PluginValidationStageResult& stage_result) const;

  const policy::ISecurityPolicyManager* security_policy_manager_;
  const IPluginPolicyGate* policy_gate_;
  PluginValidationStageCallback signature_stage_;
  PluginValidationStageCallback compatibility_stage_;
    PluginAuditAdapter* audit_adapter_;
    std::string audit_actor_ref_;
};

}  // namespace dasall::infra::plugin