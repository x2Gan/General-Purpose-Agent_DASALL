#include "AccessPolicyGate.h"

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace dasall::access {
namespace {

[[nodiscard]] bool is_override_source_allowlisted(const std::string_view source_type) {
  static constexpr std::array<std::string_view, 3> kAllowedSourceTypes = {
      "ops_command",
      "diagnostics",
      "config_center_api",
  };
  return std::find(kAllowedSourceTypes.begin(), kAllowedSourceTypes.end(), source_type) !=
         kAllowedSourceTypes.end();
}

[[nodiscard]] AccessPolicyEvaluationResult make_denied_result(
    const std::string_view reason_code,
    const std::string_view decision_ref = "policy://access/deny") {
  AccessPolicyEvaluationResult result;
  result.allowed = false;
  result.requires_confirmation = false;
  result.decision_proof.decision = "Deny";
  result.decision_proof.policy_decision_ref = std::string(decision_ref);
  result.decision_proof.reason_code = std::string(reason_code);
  result.decision_proof.reason_description = std::string("access policy gate denied request");
  result.decision_proof.evaluated_at = std::string("policy-eval");
  result.reject_reason = std::string(reason_code);
  return result;
}

[[nodiscard]] AccessPolicyEvaluationResult make_confirmation_result(
    const std::string_view reason_code,
    const std::string_view decision_ref) {
  AccessPolicyEvaluationResult result;
  result.allowed = false;
  result.requires_confirmation = true;
  result.decision_proof.decision = "RequireConfirmation";
  result.decision_proof.policy_decision_ref = std::string(decision_ref);
  result.decision_proof.reason_code = std::string(reason_code);
  result.decision_proof.reason_description =
      std::string("sensitive operation requires confirmation");
  result.decision_proof.evaluated_at = std::string("policy-eval");
  return result;
}

[[nodiscard]] AccessPolicyEvaluationResult make_allowed_result(
    const std::string_view decision_ref) {
  AccessPolicyEvaluationResult result;
  result.allowed = true;
  result.requires_confirmation = false;
  result.decision_proof.decision = "Allow";
  result.decision_proof.policy_decision_ref = std::string(decision_ref);
  result.decision_proof.reason_code = "allow_proof";
  result.decision_proof.reason_description =
      std::string("explicit allow proof emitted by access policy gate");
  result.decision_proof.evaluated_at = std::string("policy-eval");
  return result;
}

}  // namespace

bool OperationTargetView::has_consistent_values() const {
  return !operation.empty() && !target_type.empty() && !target_ref.empty();
}

bool OverrideSourceFact::has_consistent_values() const {
  return !source_type.empty() && has_config_patch_metadata && path_op_summary_complete &&
         ttl_valid && target_ref_present;
}

bool AccessPolicyEvaluationResult::denied() const {
  return !allowed && !requires_confirmation;
}

AccessPolicyEvaluationResult AccessPolicyGate::evaluate_submit(
    const AccessPolicyEvaluationInput& input,
    const PolicyBackendSnapshot& backend) const {
  if (!input.authentication.authenticated) {
    return make_denied_result("authentication_required");
  }

  const auto query_context =
      build_query_context(input, "submit", "entry", input.packet.entry_type);
  if (!query_context.has_value()) {
    return make_denied_result("malformed_policy_context");
  }

  return map_policy_result(*query_context, backend, false);
}

AccessPolicyEvaluationResult AccessPolicyGate::evaluate_task_query(
    const AccessPolicyEvaluationInput& input,
    const std::string_view task_ref,
    const PolicyBackendSnapshot& backend) const {
  if (!input.authentication.authenticated) {
    return make_denied_result("authentication_required");
  }

  const auto query_context =
      build_query_context(input, "task_query", "async_task", task_ref);
  if (!query_context.has_value()) {
    return make_denied_result("malformed_policy_context");
  }

  return map_policy_result(*query_context, backend, false);
}

AccessPolicyEvaluationResult AccessPolicyGate::evaluate_override_request(
    const AccessPolicyEvaluationInput& input,
    const OverrideSourceFact& source_fact,
    const PolicyBackendSnapshot& backend) const {
  if (!input.authentication.authenticated) {
    return make_denied_result("authentication_required");
  }

  if (!source_fact.has_consistent_values() ||
      !is_override_source_allowlisted(source_fact.source_type)) {
    return make_denied_result("override_source_invalid");
  }

  const auto query_context = build_query_context(input, "runtime_override",
                                                 "runtime_policy_patch",
                                                 input.packet.packet_id);
  if (!query_context.has_value()) {
    return make_denied_result("malformed_policy_context");
  }

  return map_policy_result(*query_context, backend, true);
}

std::optional<OperationTargetView> AccessPolicyGate::build_query_context(
    const AccessPolicyEvaluationInput& input,
    const std::string_view operation,
    const std::string_view target_type,
    const std::string_view target_ref) const {
  if (operation.empty() || target_type.empty() || target_ref.empty() ||
      input.authentication.subject_identity.actor_ref.empty() ||
      input.packet.entry_type.empty()) {
    return std::nullopt;
  }

  return OperationTargetView{
      .operation = std::string(operation),
      .target_type = std::string(target_type),
      .target_ref = std::string(target_ref),
  };
}

AccessPolicyEvaluationResult AccessPolicyGate::map_policy_result(
    const OperationTargetView& query_context,
    const PolicyBackendSnapshot& backend,
    const bool sensitive_request) const {
  if (!query_context.has_consistent_values()) {
    return make_denied_result("malformed_policy_context");
  }

  if (!backend.backend_available) {
    return make_denied_result("policy_backend_unavailable");
  }

  const bool allow_result =
      (query_context.operation == "submit" && backend.allow_submit) ||
      (query_context.operation == "task_query" && backend.allow_task_query) ||
      (query_context.operation == "runtime_override" && backend.allow_override);

  if (allow_result) {
    return make_allowed_result(backend.decision_ref);
  }

  if (sensitive_request && backend.require_confirmation_for_override) {
    return make_confirmation_result("confirmation_required", backend.decision_ref);
  }

  return make_denied_result("authorization_denied", backend.decision_ref);
}

}  // namespace dasall::access
