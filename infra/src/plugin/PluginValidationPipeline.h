#pragma once

#include <functional>
#include <string>

#include "plugin/IPluginManager.h"
#include "plugin/IPluginPolicyGate.h"
#include "plugin/PluginErrorCode.h"
#include "policy/ISecurityPolicyManager.h"

namespace dasall::infra::plugin {

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
      PluginValidationStageCallback compatibility_stage = {});

  [[nodiscard]] PluginValidationResult validate(
      const PluginValidationRequest& request) const;

 private:
  [[nodiscard]] PluginPolicyRequest make_policy_request(
      const PluginValidationRequest& request) const;
  [[nodiscard]] PluginValidationStageResult run_signature_stage(
      const PluginValidationRequest& request) const;
  [[nodiscard]] PluginValidationStageResult run_compatibility_stage(
      const PluginValidationRequest& request) const;

  const policy::ISecurityPolicyManager* security_policy_manager_;
  const IPluginPolicyGate* policy_gate_;
  PluginValidationStageCallback signature_stage_;
  PluginValidationStageCallback compatibility_stage_;
};

}  // namespace dasall::infra::plugin