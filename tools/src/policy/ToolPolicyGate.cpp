#include "ToolPolicyGate.h"

#include <algorithm>

namespace dasall::tools::policy {

namespace {

[[nodiscard]] tools::ToolAdmissionDecision deny(
    std::string reason_code,
    bool confirmation_required = false,
    bool retryable = false) {
  return tools::ToolAdmissionDecision{
      .effect = tools::ToolAdmissionEffect::deny,
      .reason_code = std::move(reason_code),
      .confirmation_required = confirmation_required,
      .retryable = retryable,
  };
}

[[nodiscard]] tools::ToolAdmissionDecision allow() {
  return tools::ToolAdmissionDecision{
      .effect = tools::ToolAdmissionEffect::allow,
      .reason_code = "policy.allowed",
      .confirmation_required = false,
      .retryable = false,
  };
}

}  // namespace

tools::ToolAdmissionDecision ToolPolicyGate::evaluate(
    const tools::ToolAdmissionRequest& request,
    const tools::ToolPolicyView& policy_view) {
  if (const auto decision = check_policy_view(policy_view); decision.has_value()) {
    return *decision;
  }
  if (const auto decision = check_safe_mode(request, policy_view); decision.has_value()) {
    return *decision;
  }
  if (const auto decision = check_allowed_domain(request, policy_view); decision.has_value()) {
    return *decision;
  }
  if (const auto decision = check_visibility(request, policy_view); decision.has_value()) {
    return *decision;
  }
  if (const auto decision = check_confirmation(request, policy_view); decision.has_value()) {
    return *decision;
  }

  return allow();
}

std::optional<tools::ToolAdmissionDecision> ToolPolicyGate::check_policy_view(
    const tools::ToolPolicyView& policy_view) const {
  if (policy_view.effective_profile_id.empty() || policy_view.audit_level.empty() ||
      policy_view.allowed_tool_domains.empty() || policy_view.tool_visibility_rules.empty()) {
    return deny("policy.profile_missing");
  }

  return std::nullopt;
}

std::optional<tools::ToolAdmissionDecision> ToolPolicyGate::check_allowed_domain(
    const tools::ToolAdmissionRequest& request,
    const tools::ToolPolicyView& policy_view) const {
  if (!request.caller_domain.has_value() || request.caller_domain->empty()) {
    return deny("policy.domain_missing");
  }

  const auto allowed = std::find(
      policy_view.allowed_tool_domains.begin(),
      policy_view.allowed_tool_domains.end(),
      *request.caller_domain);
  if (allowed == policy_view.allowed_tool_domains.end()) {
    return deny("policy.domain_denied");
  }

  return std::nullopt;
}

std::optional<tools::ToolAdmissionDecision> ToolPolicyGate::check_visibility(
    const tools::ToolAdmissionRequest& request,
    const tools::ToolPolicyView& policy_view) const {
  const auto visible = std::any_of(
      policy_view.tool_visibility_rules.begin(),
      policy_view.tool_visibility_rules.end(),
      [&](const std::string& rule) { return matches_visibility_rule(request, rule); });
  if (!visible) {
    return deny("policy.visibility_denied");
  }

  return std::nullopt;
}

std::optional<tools::ToolAdmissionDecision> ToolPolicyGate::check_confirmation(
    const tools::ToolAdmissionRequest& request,
    const tools::ToolPolicyView& policy_view) const {
  if (request.high_risk && policy_view.high_risk_confirmation_required &&
      !request.confirmation_present) {
    return deny("policy.confirmation_required", true, true);
  }

  return std::nullopt;
}

std::optional<tools::ToolAdmissionDecision> ToolPolicyGate::check_safe_mode(
    const tools::ToolAdmissionRequest& request,
    const tools::ToolPolicyView& policy_view) const {
  if (policy_view.safe_mode_enabled && !request.route_proven) {
    return deny("policy.safe_mode_route_unproven", false, true);
  }

  return std::nullopt;
}

bool ToolPolicyGate::matches_visibility_rule(
    const tools::ToolAdmissionRequest& request,
    std::string_view rule) const {
  const auto separator = rule.find(':');
  if (separator == std::string_view::npos || !request.caller_domain.has_value()) {
    return false;
  }

  const auto domain = rule.substr(0, separator);
  const auto selector = rule.substr(separator + 1U);
  if (domain != *request.caller_domain) {
    return false;
  }

  if (selector == "all") {
    return true;
  }
  if (selector == "trusted") {
    return request.route_proven;
  }
  if (selector == request.tool_name) {
    return true;
  }

  return std::find(
             request.required_scopes.begin(),
             request.required_scopes.end(),
             std::string(selector)) != request.required_scopes.end();
}

}  // namespace dasall::tools::policy