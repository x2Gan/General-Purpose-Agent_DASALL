#include <exception>
#include <iostream>
#include <string>

#include "policy/ToolPolicyGate.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] dasall::tools::ToolPolicyView make_policy_view() {
  return dasall::tools::ToolPolicyView{
      .effective_profile_id = "desktop_full",
      .safe_mode_enabled = true,
      .high_risk_confirmation_required = true,
      .audit_level = "full",
      .allowed_tool_domains = {"builtin", "mcp"},
      .tool_visibility_rules = {"builtin:all", "mcp:trusted"},
  };
}

void test_missing_profile_view_fails_closed() {
  dasall::tools::policy::ToolPolicyGate gate;
  const dasall::tools::ToolAdmissionRequest request{
      .tool_name = "echo",
      .required_scopes = {},
      .caller_domain = std::string("builtin"),
      .high_risk = false,
      .confirmation_present = false,
      .route_proven = true,
  };

  const auto decision = gate.evaluate(request, dasall::tools::ToolPolicyView{});
  assert_true(!decision.allowed(), "missing policy view should fail closed");
  assert_equal(std::string("policy.profile_missing"), decision.reason_code,
               "missing policy view should emit the profile missing reason");
}

void test_missing_confirmation_is_denied_for_high_risk_tool() {
  dasall::tools::policy::ToolPolicyGate gate;
  const auto decision = gate.evaluate(
      dasall::tools::ToolAdmissionRequest{
          .tool_name = "echo",
          .required_scopes = {},
          .caller_domain = std::string("builtin"),
          .high_risk = true,
          .confirmation_present = false,
          .route_proven = true,
      },
      make_policy_view());

  assert_true(!decision.allowed(), "high-risk request without confirmation should be denied");
  assert_equal(std::string("policy.confirmation_required"), decision.reason_code,
               "missing confirmation should emit the confirmation-required reason");
  assert_true(decision.confirmation_required,
              "missing confirmation should mark confirmation_required");
}

void test_safe_mode_rejects_unproven_route() {
  dasall::tools::policy::ToolPolicyGate gate;
  const auto decision = gate.evaluate(
      dasall::tools::ToolAdmissionRequest{
          .tool_name = "echo",
          .required_scopes = {},
          .caller_domain = std::string("builtin"),
          .high_risk = false,
          .confirmation_present = false,
          .route_proven = false,
      },
      make_policy_view());

  assert_true(!decision.allowed(), "safe mode should reject requests without a proven route");
  assert_equal(std::string("policy.safe_mode_route_unproven"), decision.reason_code,
               "safe mode denial should emit the route-unproven reason");
}

void test_allowed_caller_domain_and_visibility_are_admitted() {
  dasall::tools::policy::ToolPolicyGate gate;
  const auto decision = gate.evaluate(
      dasall::tools::ToolAdmissionRequest{
          .tool_name = "echo",
          .required_scopes = {},
          .caller_domain = std::string("builtin"),
          .high_risk = false,
          .confirmation_present = false,
          .route_proven = true,
      },
      make_policy_view());

  assert_true(decision.allowed(),
              "caller domains admitted by the Tools policy view should pass the upstream owner gate");
  assert_equal(std::string("policy.allowed"), decision.reason_code,
               "allowed caller-domain requests should preserve the allow reason");
}

void test_missing_caller_domain_is_denied_by_tools_policy_gate() {
  dasall::tools::policy::ToolPolicyGate gate;
  const auto decision = gate.evaluate(
      dasall::tools::ToolAdmissionRequest{
          .tool_name = "echo",
          .required_scopes = {},
          .caller_domain = std::nullopt,
          .high_risk = false,
          .confirmation_present = false,
          .route_proven = true,
      },
      make_policy_view());

  assert_true(!decision.allowed(),
              "missing caller domains should fail closed before any services request can be built");
  assert_equal(std::string("policy.domain_missing"), decision.reason_code,
               "missing caller-domain denial should emit the domain-missing reason");
}

void test_visibility_or_domain_denial_is_binary() {
  dasall::tools::policy::ToolPolicyGate gate;
  const auto domain_denied = gate.evaluate(
      dasall::tools::ToolAdmissionRequest{
          .tool_name = "remote.echo",
          .required_scopes = {},
          .caller_domain = std::string("workflow"),
          .high_risk = false,
          .confirmation_present = false,
          .route_proven = true,
      },
      make_policy_view());
  const auto visibility_denied = gate.evaluate(
      dasall::tools::ToolAdmissionRequest{
          .tool_name = "remote.echo",
          .required_scopes = {},
          .caller_domain = std::string("mcp"),
          .high_risk = false,
          .confirmation_present = false,
          .route_proven = false,
      },
      dasall::tools::ToolPolicyView{
          .effective_profile_id = "desktop_full",
          .safe_mode_enabled = false,
          .high_risk_confirmation_required = true,
          .audit_level = "full",
          .allowed_tool_domains = {"builtin", "mcp"},
          .tool_visibility_rules = {"builtin:all"},
      });

  assert_true(!domain_denied.allowed(), "domain outside the allowlist should be denied");
  assert_equal(std::string("policy.domain_denied"), domain_denied.reason_code,
               "domain denial should emit the domain-denied reason");
  assert_true(!visibility_denied.allowed(), "missing visibility rule should be denied");
  assert_equal(std::string("policy.visibility_denied"), visibility_denied.reason_code,
               "visibility denial should emit the visibility-denied reason");
}

}  // namespace

int main() {
  try {
    test_missing_profile_view_fails_closed();
    test_missing_confirmation_is_denied_for_high_risk_tool();
    test_safe_mode_rejects_unproven_route();
    test_allowed_caller_domain_and_visibility_are_admitted();
    test_missing_caller_domain_is_denied_by_tools_policy_gate();
    test_visibility_or_domain_denial_is_binary();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}