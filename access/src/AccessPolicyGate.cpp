#include "AccessPolicyGate.h"

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>

#include "policy/ISecurityPolicyManager.h"

namespace dasall::access {
namespace {

constexpr char kAccessPolicyUnavailableRef[] = "policy://access/backend-unavailable";

[[nodiscard]] AccessPolicyEvaluationResult make_denied_result(
  std::string_view reason_code,
  std::string_view decision_ref = "policy://access/deny");

[[nodiscard]] AccessPolicyEvaluationResult make_confirmation_result(
  std::string_view reason_code,
  std::string_view decision_ref);

[[nodiscard]] AccessPolicyEvaluationResult make_allowed_result(
  std::string_view decision_ref);

class SnapshotPolicyEvaluator final : public IAccessPolicyEvaluator {
 public:
  explicit SnapshotPolicyEvaluator(PolicyBackendSnapshot backend)
      : backend_(std::move(backend)) {}

  [[nodiscard]] AccessPolicyEvaluationResult evaluate(
      const AccessPolicyQuery& query) const override {
    if (!query.has_consistent_values()) {
      return make_denied_result("malformed_policy_context");
    }

    if (!backend_.backend_available) {
      return make_denied_result("policy_backend_unavailable");
    }

    const bool allow_result =
        (query.operation_target.operation == "submit" && backend_.allow_submit) ||
        (query.operation_target.operation == "task_query" && backend_.allow_task_query) ||
        (query.operation_target.operation == "diagnostics_read" &&
         backend_.allow_diagnostics) ||
        (query.operation_target.operation == "runtime_override" &&
         backend_.allow_override);

    if (allow_result) {
      return make_allowed_result(backend_.decision_ref);
    }

    if (query.sensitive_request && backend_.require_confirmation_for_override) {
      return make_confirmation_result("confirmation_required", backend_.decision_ref);
    }

    return make_denied_result("authorization_denied", backend_.decision_ref);
  }

 private:
  PolicyBackendSnapshot backend_;
};

[[nodiscard]] std::string build_policy_ref_from_decision(
    const dasall::infra::policy::PolicyDecisionRef& decision) {
  std::string policy_ref = std::string("policy://snapshots/") + decision.snapshot_id +
                           "?generation=" + std::to_string(decision.generation);
  if (!decision.matched_rule_ids.empty()) {
    policy_ref += "#" + decision.matched_rule_ids.front();
  }
  return policy_ref;
}

[[nodiscard]] std::string build_policy_module(const AccessPolicyQuery& query) {
  return std::string("access.") + query.entry_type + "." + query.protocol_kind;
}

[[nodiscard]] AccessPolicyEvaluationResult map_policy_manager_decision(
    const dasall::infra::policy::PolicyDecisionRef& decision) {
  if (!decision.is_valid()) {
    return make_denied_result("policy_backend_unavailable", kAccessPolicyUnavailableRef);
  }

  AccessPolicyEvaluationResult result;
  result.allowed = decision.decision == dasall::infra::policy::PolicyDecision::Allow;
  result.requires_confirmation =
      decision.decision == dasall::infra::policy::PolicyDecision::RequireConfirmation;

  switch (decision.decision) {
    case dasall::infra::policy::PolicyDecision::Allow:
      result.decision_proof.decision = "Allow";
      break;
    case dasall::infra::policy::PolicyDecision::Deny:
      result.decision_proof.decision = "Deny";
      result.reject_reason = decision.reason_code;
      break;
    case dasall::infra::policy::PolicyDecision::RequireConfirmation:
      result.decision_proof.decision = "RequireConfirmation";
      break;
    case dasall::infra::policy::PolicyDecision::Unspecified:
      return make_denied_result("policy_backend_unavailable", kAccessPolicyUnavailableRef);
  }

  result.decision_proof.policy_decision_ref = build_policy_ref_from_decision(decision);
  result.decision_proof.reason_code = decision.reason_code;
  if (!decision.evidence_ref.empty()) {
    result.decision_proof.reason_description = decision.evidence_ref;
  }
  result.decision_proof.evaluated_at = std::string("policy-manager");
  return result;
}

class InfraPolicyEvaluator final : public IAccessPolicyEvaluator {
 public:
  explicit InfraPolicyEvaluator(
      std::shared_ptr<dasall::infra::policy::ISecurityPolicyManager> policy_manager)
      : policy_manager_(std::move(policy_manager)) {}

  [[nodiscard]] AccessPolicyEvaluationResult evaluate(
      const AccessPolicyQuery& query) const override {
    if (!policy_manager_ || !query.has_consistent_values()) {
      return make_denied_result("policy_backend_unavailable", kAccessPolicyUnavailableRef);
    }

    const auto profile_id =
        query.snapshot_fingerprint.has_value() &&
                !query.snapshot_fingerprint->effective_profile_id.empty()
            ? query.snapshot_fingerprint->effective_profile_id
            : std::string("unknown");
    const auto task_id = query.operation_target.target_type == "async_task"
                             ? query.operation_target.target_ref
                             : std::string("unknown");

    const auto decision = policy_manager_->evaluate(
        dasall::infra::policy::PolicyQueryContext{
            .module = build_policy_module(query),
            .operation = query.operation_target.operation,
            .target_type = query.operation_target.target_type,
            .target_ref = query.operation_target.target_ref,
            .actor_ref = query.subject_identity.actor_ref,
            .request_id = query.request_id,
            .session_id = query.session_id,
            .trace_id = query.trace_id,
            .task_id = task_id,
            .profile_id = profile_id,
        });
    return map_policy_manager_decision(decision);
  }

 private:
  std::shared_ptr<dasall::infra::policy::ISecurityPolicyManager> policy_manager_;
};

[[nodiscard]] bool is_override_source_allowlisted(const std::string_view source_type) {
  static constexpr std::array<std::string_view, 3> kAllowedSourceTypes = {
      "ops_command",
      "diagnostics",
      "config_center_api",
  };
  return std::find(kAllowedSourceTypes.begin(), kAllowedSourceTypes.end(), source_type) !=
         kAllowedSourceTypes.end();
}

[[nodiscard]] bool is_local_trusted_daemon_subject(
    const AccessPolicyEvaluationInput& input) {
  if (input.packet.entry_type != "daemon") {
    return true;
  }

  const auto& subject = input.authentication.subject_identity;
  if (subject.auth_method != "local_trusted") {
    return false;
  }

  if (subject.actor_ref.empty()) {
    return false;
  }

  return subject.actor_ref.rfind("local://uid/", 0U) == 0U;
}

[[nodiscard]] AccessPolicyEvaluationResult make_denied_result(
    const std::string_view reason_code,
  const std::string_view decision_ref) {
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

bool AccessPolicyQuery::has_consistent_values() const {
  return operation_target.has_consistent_values() && !subject_identity.actor_ref.empty() &&
         !entry_type.empty() && !protocol_kind.empty() && !request_id.empty();
}

AccessPolicyGate::AccessPolicyGate(
    std::shared_ptr<const IAccessPolicyEvaluator> policy_evaluator)
    : policy_evaluator_(std::move(policy_evaluator)) {}

AccessPolicyEvaluationResult AccessPolicyGate::evaluate_submit(
    const AccessPolicyEvaluationInput& input) const {
  if (!policy_evaluator_) {
    return make_denied_result("policy_backend_unavailable");
  }

  return evaluate_with_evaluator(
      input, "submit", "entry", input.packet.entry_type, false, false, nullptr,
      *policy_evaluator_);
}

AccessPolicyEvaluationResult AccessPolicyGate::evaluate_submit(
    const AccessPolicyEvaluationInput& input,
    const PolicyBackendSnapshot& backend) const {
  SnapshotPolicyEvaluator evaluator(backend);
  return evaluate_with_evaluator(
      input, "submit", "entry", input.packet.entry_type, false, false, nullptr,
      evaluator);
}

AccessPolicyEvaluationResult AccessPolicyGate::evaluate_task_query(
    const AccessPolicyEvaluationInput& input,
    const std::string_view task_ref) const {
  if (!policy_evaluator_) {
    return make_denied_result("policy_backend_unavailable");
  }

  return evaluate_with_evaluator(
      input, "task_query", "async_task", task_ref, false, false, nullptr,
      *policy_evaluator_);
}

AccessPolicyEvaluationResult AccessPolicyGate::evaluate_task_query(
    const AccessPolicyEvaluationInput& input,
    const std::string_view task_ref,
    const PolicyBackendSnapshot& backend) const {
  SnapshotPolicyEvaluator evaluator(backend);
  return evaluate_with_evaluator(
      input, "task_query", "async_task", task_ref, false, false, nullptr,
      evaluator);
}

AccessPolicyEvaluationResult AccessPolicyGate::evaluate_diagnostics_request(
    const AccessPolicyEvaluationInput& input,
    const std::string_view command_name) const {
  if (!policy_evaluator_) {
    return make_denied_result("policy_backend_unavailable");
  }

  return evaluate_with_evaluator(
      input, "diagnostics_read", "diagnostics_command", command_name, false, true,
      nullptr, *policy_evaluator_);
}

AccessPolicyEvaluationResult AccessPolicyGate::evaluate_diagnostics_request(
    const AccessPolicyEvaluationInput& input,
    const std::string_view command_name,
    const PolicyBackendSnapshot& backend) const {
  SnapshotPolicyEvaluator evaluator(backend);
  return evaluate_with_evaluator(
      input, "diagnostics_read", "diagnostics_command", command_name, false, true,
      nullptr, evaluator);
}

AccessPolicyEvaluationResult AccessPolicyGate::evaluate_override_request(
    const AccessPolicyEvaluationInput& input,
    const OverrideSourceFact& source_fact) const {
  if (!policy_evaluator_) {
    return make_denied_result("policy_backend_unavailable");
  }

  return evaluate_with_evaluator(
      input, "runtime_override", "runtime_policy_patch", input.packet.packet_id,
      true, true, &source_fact, *policy_evaluator_);
}

AccessPolicyEvaluationResult AccessPolicyGate::evaluate_override_request(
    const AccessPolicyEvaluationInput& input,
    const OverrideSourceFact& source_fact,
    const PolicyBackendSnapshot& backend) const {
  SnapshotPolicyEvaluator evaluator(backend);
  return evaluate_with_evaluator(
      input, "runtime_override", "runtime_policy_patch", input.packet.packet_id,
      true, true, &source_fact, evaluator);
}

std::optional<AccessPolicyQuery> AccessPolicyGate::build_query_context(
    const AccessPolicyEvaluationInput& input,
    const std::string_view operation,
    const std::string_view target_type,
    const std::string_view target_ref,
    const bool sensitive_request) const {
  if (operation.empty() || target_type.empty() || target_ref.empty() ||
      input.authentication.subject_identity.actor_ref.empty() ||
      input.packet.entry_type.empty() || input.packet.protocol_kind.empty()) {
    return std::nullopt;
  }

  return AccessPolicyQuery{
      .subject_identity = input.authentication.subject_identity,
      .operation_target = OperationTargetView{
          .operation = std::string(operation),
          .target_type = std::string(target_type),
          .target_ref = std::string(target_ref),
      },
      .entry_type = input.packet.entry_type,
      .protocol_kind = input.packet.protocol_kind,
      .request_id = input.packet.packet_id.empty() ? std::string("unknown")
                                                   : input.packet.packet_id,
      .session_id = input.packet.session_hint.value_or(std::string("unknown")),
      .trace_id = input.packet.trace_id.value_or(std::string("unknown")),
      .snapshot_fingerprint = input.snapshot_fingerprint,
      .sensitive_request = sensitive_request,
  };
}

AccessPolicyEvaluationResult AccessPolicyGate::evaluate_with_evaluator(
    const AccessPolicyEvaluationInput& input,
    const std::string_view operation,
    const std::string_view target_type,
    const std::string_view target_ref,
    const bool sensitive_request,
    const bool require_local_trusted,
    const OverrideSourceFact* source_fact,
    const IAccessPolicyEvaluator& evaluator) const {
  if (!input.authentication.authenticated) {
    return make_denied_result("authentication_required");
  }

  if (source_fact != nullptr &&
      (!source_fact->has_consistent_values() ||
       !is_override_source_allowlisted(source_fact->source_type))) {
    return make_denied_result("override_source_invalid");
  }

  if (require_local_trusted && !is_local_trusted_daemon_subject(input)) {
    return make_denied_result("daemon_peer_identity_required");
  }

  const auto query_context =
      build_query_context(input, operation, target_type, target_ref, sensitive_request);
  if (!query_context.has_value()) {
    return make_denied_result("malformed_policy_context");
  }

  return evaluator.evaluate(*query_context);
}

std::shared_ptr<const IAccessPolicyEvaluator> make_infra_policy_evaluator(
    std::shared_ptr<dasall::infra::policy::ISecurityPolicyManager> policy_manager) {
  if (!policy_manager) {
    return nullptr;
  }

  return std::make_shared<InfraPolicyEvaluator>(std::move(policy_manager));
}

}  // namespace dasall::access
