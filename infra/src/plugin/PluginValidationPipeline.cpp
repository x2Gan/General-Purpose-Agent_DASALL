#include "plugin/PluginValidationPipeline.h"

#include <string_view>
#include <utility>

#include "plugin/PluginAuditAdapter.h"

namespace dasall::infra::plugin {
namespace {

constexpr std::string_view kPluginValidationSourceRef = "PluginValidationPipeline";
constexpr std::string_view kValidationEvidencePrefix = "plugin.validation://";
constexpr std::string_view kSignatureReportPrefix = "plugin.signature://";
constexpr std::string_view kCompatibilityReportPrefix = "plugin.compatibility://";

[[nodiscard]] std::string normalized_or_unknown(std::string value) {
  return plugin_value_or_unknown(value);
}

[[nodiscard]] std::string build_ref(std::string_view prefix,
                                    std::string_view plugin_id,
                                    std::string_view suffix) {
  return std::string(prefix) + std::string(plugin_id) + "/" + std::string(suffix);
}

[[nodiscard]] std::string build_message(PluginErrorCode error_code,
                                        std::string_view detail) {
  return std::string(plugin_error_code_name(error_code)) + ": " + std::string(detail);
}

[[nodiscard]] PluginValidationResult make_failure_result(
    PluginErrorCode error_code,
    const PluginValidationRequest& request,
    std::string message,
    std::string stage,
    std::string evidence_ref,
    policy::PolicyDecisionRef policy_decision = {},
    std::string signature_report_ref = {},
    std::string compatibility_report_ref = {}) {
  auto result = PluginValidationResult::failure(map_plugin_error_code(error_code).result_code,
                                                request.plugin_id,
                                                std::move(message),
                                                std::move(stage),
                                                std::string(kPluginValidationSourceRef),
                                                std::move(evidence_ref));
  result.policy_decision = std::move(policy_decision);
  result.signature_report_ref = std::move(signature_report_ref);
  result.compatibility_report_ref = std::move(compatibility_report_ref);
  return result;
}

[[nodiscard]] PluginValidationStageResult default_signature_stage(
    const PluginValidationRequest& request) {
  return PluginValidationStageResult::success(
      build_ref(kSignatureReportPrefix,
                normalized_or_unknown(request.plugin_id),
                "skeleton-pass"),
      build_ref(kValidationEvidencePrefix,
                normalized_or_unknown(request.plugin_id),
                "signature/skeleton-pass"),
      "plugin_signature_skeleton_pass");
}

[[nodiscard]] PluginValidationStageResult default_compatibility_stage(
    const PluginValidationRequest& request) {
  return PluginValidationStageResult::success(
      build_ref(kCompatibilityReportPrefix,
                normalized_or_unknown(request.plugin_id),
                "skeleton-pass"),
      build_ref(kValidationEvidencePrefix,
                normalized_or_unknown(request.plugin_id),
                "compatibility/skeleton-pass"),
      "plugin_compatibility_skeleton_pass");
}

}  // namespace

PluginValidationStageResult PluginValidationStageResult::success(
    std::string report_ref,
    std::string evidence_ref,
    std::string reason_code) {
  return PluginValidationStageResult{
      .passed = true,
      .report_ref = std::move(report_ref),
      .evidence_ref = std::move(evidence_ref),
      .error_code = PluginErrorCode::ValidateFail,
      .reason_code = std::move(reason_code),
      .message = {},
  };
}

PluginValidationStageResult PluginValidationStageResult::failure(
    PluginErrorCode error_code,
    std::string report_ref,
    std::string evidence_ref,
    std::string reason_code,
    std::string message) {
  return PluginValidationStageResult{
      .passed = false,
      .report_ref = std::move(report_ref),
      .evidence_ref = std::move(evidence_ref),
      .error_code = error_code,
      .reason_code = std::move(reason_code),
      .message = std::move(message),
  };
}

bool PluginValidationStageResult::is_valid() const {
  if (report_ref.empty() || evidence_ref.empty()) {
    return false;
  }

  if (passed) {
    return true;
  }

  return !reason_code.empty() && !message.empty();
}

PluginValidationPipeline::PluginValidationPipeline(
    const policy::ISecurityPolicyManager* security_policy_manager,
    const IPluginPolicyGate* policy_gate,
    PluginValidationStageCallback signature_stage,
    PluginValidationStageCallback compatibility_stage,
    PluginAuditAdapter* audit_adapter,
    std::string audit_actor_ref)
    : security_policy_manager_(security_policy_manager),
      policy_gate_(policy_gate),
      signature_stage_(std::move(signature_stage)),
      compatibility_stage_(std::move(compatibility_stage)),
      audit_adapter_(audit_adapter),
      audit_actor_ref_(normalized_or_unknown(std::move(audit_actor_ref))) {}

PluginValidationResult PluginValidationPipeline::validate(
    const PluginValidationRequest& request) const {
  if (!request.is_valid()) {
    return make_failure_result(
        PluginErrorCode::ValidateFail,
        request,
        build_message(PluginErrorCode::ValidateFail,
                      "plugin validation request requires plugin_id, manifest_ref, package_ref, and profile_id"),
        "plugin.validate.input",
        build_ref(kValidationEvidencePrefix,
                  normalized_or_unknown(request.plugin_id),
                  "input/invalid"));
  }

  if (security_policy_manager_ == nullptr || policy_gate_ == nullptr) {
    return make_failure_result(
        PluginErrorCode::ValidateFail,
        request,
        build_message(PluginErrorCode::ValidateFail,
                      "plugin validation pipeline requires security policy manager and policy gate dependencies"),
        "plugin.validate.policy",
        build_ref(kValidationEvidencePrefix,
                  request.plugin_id,
                  "policy/misconfigured"));
  }

  const auto policy_snapshot = security_policy_manager_->snapshot();
  if (!policy_snapshot.is_valid()) {
    return make_failure_result(
        PluginErrorCode::ValidateFail,
        request,
        build_message(PluginErrorCode::ValidateFail,
                      "plugin validation pipeline requires a valid policy snapshot before evaluation"),
        "plugin.validate.policy",
        build_ref(kValidationEvidencePrefix,
                  request.plugin_id,
                  "policy/invalid-snapshot"));
  }

  const auto policy_request = make_policy_request(request);
  if (!policy_request.is_valid()) {
    return make_failure_result(
        PluginErrorCode::ValidateFail,
        request,
        build_message(PluginErrorCode::ValidateFail,
                      "plugin validation pipeline could not derive a valid policy request from the frozen validation request"),
        "plugin.validate.policy",
        build_ref(kValidationEvidencePrefix,
                  request.plugin_id,
                  "policy/invalid-request"));
  }

  const auto policy_decision = policy_gate_->evaluate(policy_request, policy_snapshot);
  if (!policy_decision.is_valid()) {
    return make_failure_result(
        PluginErrorCode::ValidateFail,
        request,
        build_message(PluginErrorCode::ValidateFail,
                      "plugin policy gate returned an invalid PolicyDecisionRef"),
        "plugin.validate.policy",
        build_ref(kValidationEvidencePrefix,
                  request.plugin_id,
                  "policy/invalid-decision"));
  }

  if (policy_decision.decision != policy::PolicyDecision::Allow) {
    emit_policy_deny_audit(request, policy_decision);
    return make_failure_result(
        PluginErrorCode::PolicyDenied,
        request,
        build_message(PluginErrorCode::PolicyDenied,
                      policy_decision.reason_code.empty()
                          ? "plugin validation rejected by policy gate"
                          : policy_decision.reason_code),
        "plugin.validate.policy",
        policy_decision.evidence_ref,
        policy_decision);
  }

  const auto signature_result = run_signature_stage(request);
  if (!signature_result.is_valid()) {
    return make_failure_result(
        PluginErrorCode::ValidateFail,
        request,
        build_message(PluginErrorCode::ValidateFail,
                      "plugin validation signature stage returned an invalid stage result"),
        "plugin.validate.signature",
        build_ref(kValidationEvidencePrefix,
                  request.plugin_id,
                  "signature/invalid-stage-result"),
        policy_decision);
  }

  if (!signature_result.passed) {
    emit_stage_failure_audit(request, signature_result);
    return make_failure_result(PluginErrorCode::SignatureFail,
                               request,
                               signature_result.message,
                               "plugin.validate.signature",
                               signature_result.evidence_ref,
                               policy_decision,
                               signature_result.report_ref);
  }

  const auto compatibility_result = run_compatibility_stage(request);
  if (!compatibility_result.is_valid()) {
    return make_failure_result(
        PluginErrorCode::ValidateFail,
        request,
        build_message(PluginErrorCode::ValidateFail,
                      "plugin validation compatibility stage returned an invalid stage result"),
        "plugin.validate.compatibility",
        build_ref(kValidationEvidencePrefix,
                  request.plugin_id,
                  "compatibility/invalid-stage-result"),
        policy_decision,
        signature_result.report_ref);
  }

  if (!compatibility_result.passed) {
    emit_stage_failure_audit(request, compatibility_result);
    return make_failure_result(PluginErrorCode::CompatibilityFail,
                               request,
                               compatibility_result.message,
                               "plugin.validate.compatibility",
                               compatibility_result.evidence_ref,
                               policy_decision,
                               signature_result.report_ref,
                               compatibility_result.report_ref);
  }

  return PluginValidationResult::success(
      request.plugin_id,
      policy_decision,
      signature_result.report_ref,
      compatibility_result.report_ref,
      build_ref(kValidationEvidencePrefix, request.plugin_id, "accepted"));
}

PluginPolicyRequest PluginValidationPipeline::make_policy_request(
    const PluginValidationRequest& request) const {
  return PluginPolicyRequest{
      .descriptor = PluginDescriptor::normalize(PluginDescriptor{
          .plugin_id = request.plugin_id,
          .version = std::string("pending.version"),
          .abi = std::string("pending.abi"),
          .trust_level = PluginTrustLevel::Untrusted,
          .status = PluginStatus::Discovered,
          .source = request.package_ref,
      }),
      .manifest_ref = request.manifest_ref,
      .profile_id = request.profile_id,
  };
}

PluginValidationStageResult PluginValidationPipeline::run_signature_stage(
    const PluginValidationRequest& request) const {
  if (signature_stage_) {
    return signature_stage_(request);
  }

  return default_signature_stage(request);
}

PluginValidationStageResult PluginValidationPipeline::run_compatibility_stage(
    const PluginValidationRequest& request) const {
  if (compatibility_stage_) {
    return compatibility_stage_(request);
  }

  return default_compatibility_stage(request);
}

void PluginValidationPipeline::emit_policy_deny_audit(
    const PluginValidationRequest& request,
    const policy::PolicyDecisionRef& policy_decision) const {
  if (audit_adapter_ == nullptr || !policy_decision.is_valid()) {
    return;
  }

  PluginAuditRecord record{
      .actor_ref = audit_actor_ref_,
      .plugin_id = request.plugin_id,
      .succeeded = false,
      .evidence_ref = policy_decision.evidence_ref,
      .reason_code = policy_decision.reason_code.empty()
                         ? std::string("plugin_policy_denied")
                         : policy_decision.reason_code,
      .result_code = map_plugin_error_code(PluginErrorCode::PolicyDenied).result_code,
      .request_id = std::nullopt,
      .trace_id = std::nullopt,
      .task_id = std::nullopt,
  };
  static_cast<void>(audit_adapter_->write_policy_deny_audit(std::move(record)));
}

void PluginValidationPipeline::emit_stage_failure_audit(
    const PluginValidationRequest& request,
    const PluginValidationStageResult& stage_result) const {
  if (audit_adapter_ == nullptr || stage_result.passed || !stage_result.is_valid()) {
    return;
  }

  PluginAuditRecord record{
      .actor_ref = audit_actor_ref_,
      .plugin_id = request.plugin_id,
      .succeeded = false,
      .evidence_ref = stage_result.evidence_ref,
      .reason_code = stage_result.reason_code.empty()
                         ? std::string(plugin_error_code_name(stage_result.error_code))
                         : stage_result.reason_code,
      .result_code = map_plugin_error_code(stage_result.error_code).result_code,
      .request_id = std::nullopt,
      .trace_id = std::nullopt,
      .task_id = std::nullopt,
  };

  switch (stage_result.error_code) {
    case PluginErrorCode::SignatureFail:
      static_cast<void>(audit_adapter_->write_signature_fail_audit(std::move(record)));
      return;
    case PluginErrorCode::CompatibilityFail:
      static_cast<void>(audit_adapter_->write_compatibility_fail_audit(std::move(record)));
      return;
    case PluginErrorCode::ValidateFail:
    case PluginErrorCode::PolicyDenied:
    case PluginErrorCode::LoadFail:
    case PluginErrorCode::UnloadFail:
      return;
  }
}

}  // namespace dasall::infra::plugin