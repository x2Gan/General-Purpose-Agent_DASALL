#include "diagnostics/CommandPolicyGuard.h"

#include <string>
#include <utility>

#include "policy/ISecurityPolicyManager.h"

namespace dasall::infra::diagnostics {
namespace {

constexpr char kDiagnosticsModule[] = "diagnostics";
constexpr char kDiagnosticsOperation[] = "execute";
constexpr char kDiagnosticsTargetType[] = "diagnostics_command";
constexpr char kInputGuardPolicyRef[] = "policy://diagnostics/input-guard";
constexpr char kContextGuardPolicyRef[] = "policy://diagnostics/context-guard";
constexpr char kInvalidDecisionPolicyRef[] = "policy://diagnostics/policy-decision-invalid";

[[nodiscard]] std::string build_policy_ref_from_decision(const policy::PolicyDecisionRef& decision) {
  std::string policy_ref = std::string("policy://snapshots/") + decision.snapshot_id +
                           "?generation=" + std::to_string(decision.generation);

  if (!decision.matched_rule_ids.empty()) {
    policy_ref += "#" + decision.matched_rule_ids.front();
  }

  return policy_ref;
}

[[nodiscard]] CommandDecision make_invalid_input_decision(std::string denied_rule_id,
                                                          std::string policy_ref) {
  return CommandDecision{
      .allowed = false,
      .reason_code = std::string("diag_command_invalid"),
      .policy_ref = std::move(policy_ref),
      .denied_rule_id = std::move(denied_rule_id),
  };
}

[[nodiscard]] CommandDecision make_denied_decision(const policy::PolicyDecisionRef& decision) {
  return CommandDecision{
      .allowed = false,
      .reason_code = std::string("diag_command_denied"),
      .policy_ref = build_policy_ref_from_decision(decision),
      .denied_rule_id = decision.matched_rule_ids.empty() ? std::string("policy-rule-unspecified")
                                                          : decision.matched_rule_ids.front(),
  };
}

[[nodiscard]] CommandDecision make_allowed_decision(const policy::PolicyDecisionRef& decision) {
  return CommandDecision{
      .allowed = true,
      .reason_code = decision.reason_code,
      .policy_ref = build_policy_ref_from_decision(decision),
      .denied_rule_id = std::string(),
  };
}

}  // namespace

CommandPolicyGuard::CommandPolicyGuard(const policy::ISecurityPolicyManager& policy_manager)
    : policy_manager_(policy_manager) {}

CommandDecision CommandPolicyGuard::authorize(const DiagnosticsCommand& command,
                                              const InfraContext& context) {
  if (!command.has_required_fields()) {
    return make_invalid_input_decision(std::string("command-metadata-incomplete"),
                                       std::string(kInputGuardPolicyRef));
  }

  if (!command.has_whitelisted_command_name()) {
    return CommandDecision{
        .allowed = false,
        .reason_code = std::string("diag_command_denied"),
        .policy_ref = std::string(kInputGuardPolicyRef),
        .denied_rule_id = std::string("readonly-only"),
    };
  }

  if (context.request_id == InfraContext::kUnknownIdentifier || context.request_id.empty()) {
    return CommandDecision{
        .allowed = false,
        .reason_code = std::string("diag_command_denied"),
        .policy_ref = std::string(kContextGuardPolicyRef),
        .denied_rule_id = std::string("missing-request-id"),
    };
  }

  const auto decision = policy_manager_.evaluate(build_query(command, context));
  if (!decision.is_valid()) {
    return make_invalid_input_decision(std::string("policy-decision-invalid"),
                                       std::string(kInvalidDecisionPolicyRef));
  }

  if (decision.decision == policy::PolicyDecision::Allow) {
    return make_allowed_decision(decision);
  }

  return make_denied_decision(decision);
}

policy::PolicyQueryContext CommandPolicyGuard::build_query(const DiagnosticsCommand& command,
                                                           const InfraContext& context) {
  return policy::PolicyQueryContext{
      .module = std::string(kDiagnosticsModule),
      .operation = std::string(kDiagnosticsOperation),
      .target_type = std::string(kDiagnosticsTargetType),
      .target_ref = command.command_name,
      .actor_ref = command.actor_ref,
      .request_id = context.request_id,
      .session_id = context.session_id,
      .trace_id = context.trace_id,
      .task_id = context.task_id,
      .profile_id = std::string("unknown"),
  };
}

std::string CommandPolicyGuard::build_policy_ref(const policy::PolicyDecisionRef& decision) {
  return build_policy_ref_from_decision(decision);
}

}  // namespace dasall::infra::diagnostics