// WP03-T003-B: AgentRequest field-level contract tests.
//
// Validates the WP03-T003 field rules enforced by
// validate_agent_request_field_rules():
//   - Optional string fields must be non-empty when present.
//   - Optional numeric fields (timeout_ms, priority) must be positive.
//   - tags must contain no empty strings.
//   - runtime_budget dimensions must each be > 0 when present.
//   - All required + boundary rules (T002-B) are inherited.

#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "agent/AgentRequest.h"
#include "agent/AgentRequestGuards.h"
#include "checkpoint/RuntimeBudget.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

using dasall::contracts::AgentRequest;
using dasall::contracts::AgentRequestGuardResult;
using dasall::contracts::RequestChannel;
using dasall::contracts::RuntimeBudget;
using dasall::contracts::validate_agent_request_field_rules;
using dasall::tests::support::assert_true;

// ---------------------------------------------------------------------------
// Helper: construct a valid AgentRequest with all required fields set.
// ---------------------------------------------------------------------------

AgentRequest make_valid_request() {
  AgentRequest req;
  req.request_id = "req-field-001";
  req.session_id = "sess-field-001";
  req.trace_id = "trace-field-001";
  req.user_input = "Hello from field contract test";
  req.request_channel = RequestChannel::Gateway;
  req.created_at = 1710000000000;
  return req;
}

// ===========================================================================
// Positive cases
// ===========================================================================

// P1: Minimal valid request (required fields only, no optional fields).
void test_minimal_valid_request_passes_field_rules() {
  auto req = make_valid_request();
  auto result = validate_agent_request_field_rules(req);
  assert_true(result.ok,
              "minimal valid request should pass field rules");
}

// P2: Valid request with all optional fields properly set.
void test_full_valid_request_passes_field_rules() {
  auto req = make_valid_request();
  req.goal_hint = "Summarize the report";
  req.domain_context = "weekly engineering sync";
  req.constraint_set = "no-external-api";
  req.approval_policy_hint = "auto-approve";
  req.idempotency_key = "idem-001";
  req.locale = "en-US";
  req.client_capabilities = "streaming,markdown";
  req.timeout_ms = 30000;
  req.priority = 3;
  req.deadline_at = 1710000060000;
  req.tags = std::vector<std::string>{"urgent", "review"};

  RuntimeBudget budget;
  budget.max_tokens = 4096;
  budget.max_turns = 10;
  budget.max_tool_calls = 20;
  budget.max_latency_ms = 60000;
  budget.max_replan_count = 3;
  req.runtime_budget = budget;

  auto result = validate_agent_request_field_rules(req);
  assert_true(result.ok,
              "full valid request with all optional fields should pass");
}

// P3: Valid request with partial optional fields (proving absence is OK).
void test_partial_optional_fields_passes() {
  auto req = make_valid_request();
  req.goal_hint = "Just a hint";
  req.timeout_ms = 5000;
  // All other optional fields remain nullopt — should pass.
  auto result = validate_agent_request_field_rules(req);
  assert_true(result.ok,
              "request with partial optional fields should pass");
}

// P4: Valid runtime_budget with only some dimensions set.
void test_partial_runtime_budget_passes() {
  auto req = make_valid_request();
  RuntimeBudget budget;
  budget.max_tokens = 2048;
  // Other dimensions remain nullopt.
  req.runtime_budget = budget;
  auto result = validate_agent_request_field_rules(req);
  assert_true(result.ok,
              "partial runtime_budget should pass field rules");
}

// ===========================================================================
// Negative cases: optional string fields present but empty
// ===========================================================================

// N1: Empty goal_hint.
void test_empty_goal_hint_fails() {
  auto req = make_valid_request();
  req.goal_hint = "";
  auto result = validate_agent_request_field_rules(req);
  assert_true(!result.ok, "empty goal_hint should fail field rules");
}

// N2: Empty domain_context.
void test_empty_domain_context_fails() {
  auto req = make_valid_request();
  req.domain_context = "";
  auto result = validate_agent_request_field_rules(req);
  assert_true(!result.ok, "empty domain_context should fail field rules");
}

// N3: Empty constraint_set.
void test_empty_constraint_set_fails() {
  auto req = make_valid_request();
  req.constraint_set = "";
  auto result = validate_agent_request_field_rules(req);
  assert_true(!result.ok, "empty constraint_set should fail field rules");
}

// N4: Empty approval_policy_hint.
void test_empty_approval_policy_hint_fails() {
  auto req = make_valid_request();
  req.approval_policy_hint = "";
  auto result = validate_agent_request_field_rules(req);
  assert_true(!result.ok,
              "empty approval_policy_hint should fail field rules");
}

// N5: Empty idempotency_key.
void test_empty_idempotency_key_fails() {
  auto req = make_valid_request();
  req.idempotency_key = "";
  auto result = validate_agent_request_field_rules(req);
  assert_true(!result.ok, "empty idempotency_key should fail field rules");
}

// N6: Empty locale.
void test_empty_locale_fails() {
  auto req = make_valid_request();
  req.locale = "";
  auto result = validate_agent_request_field_rules(req);
  assert_true(!result.ok, "empty locale should fail field rules");
}

// N7: Empty client_capabilities.
void test_empty_client_capabilities_fails() {
  auto req = make_valid_request();
  req.client_capabilities = "";
  auto result = validate_agent_request_field_rules(req);
  assert_true(!result.ok,
              "empty client_capabilities should fail field rules");
}

// ===========================================================================
// Negative cases: optional numeric fields zero
// ===========================================================================

// N8: Zero timeout_ms.
void test_zero_timeout_ms_fails() {
  auto req = make_valid_request();
  req.timeout_ms = 0;
  auto result = validate_agent_request_field_rules(req);
  assert_true(!result.ok, "zero timeout_ms should fail field rules");
}

// N9: Zero priority.
void test_zero_priority_fails() {
  auto req = make_valid_request();
  req.priority = 0;
  auto result = validate_agent_request_field_rules(req);
  assert_true(!result.ok, "zero priority should fail field rules");
}

// ===========================================================================
// Negative cases: tags violations
// ===========================================================================

// N10: Empty tags vector.
void test_empty_tags_vector_fails() {
  auto req = make_valid_request();
  req.tags = std::vector<std::string>{};
  auto result = validate_agent_request_field_rules(req);
  assert_true(!result.ok, "empty tags vector should fail field rules");
}

// N11: Tags containing an empty string.
void test_tags_with_empty_string_fails() {
  auto req = make_valid_request();
  req.tags = std::vector<std::string>{"valid-tag", ""};
  auto result = validate_agent_request_field_rules(req);
  assert_true(!result.ok,
              "tags with empty string should fail field rules");
}

// ===========================================================================
// Negative cases: runtime_budget dimension violations
// ===========================================================================

// N12: Zero max_tokens in budget.
void test_zero_budget_max_tokens_fails() {
  auto req = make_valid_request();
  RuntimeBudget budget;
  budget.max_tokens = 0;
  req.runtime_budget = budget;
  auto result = validate_agent_request_field_rules(req);
  assert_true(!result.ok,
              "zero max_tokens in budget should fail field rules");
}

// N13: Zero max_turns in budget.
void test_zero_budget_max_turns_fails() {
  auto req = make_valid_request();
  RuntimeBudget budget;
  budget.max_turns = 0;
  req.runtime_budget = budget;
  auto result = validate_agent_request_field_rules(req);
  assert_true(!result.ok,
              "zero max_turns in budget should fail field rules");
}

// N14: Zero max_tool_calls in budget.
void test_zero_budget_max_tool_calls_fails() {
  auto req = make_valid_request();
  RuntimeBudget budget;
  budget.max_tool_calls = 0;
  req.runtime_budget = budget;
  auto result = validate_agent_request_field_rules(req);
  assert_true(!result.ok,
              "zero max_tool_calls in budget should fail field rules");
}

// N15: Zero max_latency_ms in budget.
void test_zero_budget_max_latency_ms_fails() {
  auto req = make_valid_request();
  RuntimeBudget budget;
  budget.max_latency_ms = 0;
  req.runtime_budget = budget;
  auto result = validate_agent_request_field_rules(req);
  assert_true(!result.ok,
              "zero max_latency_ms in budget should fail field rules");
}

// N16: Zero max_replan_count in budget.
void test_zero_budget_max_replan_count_fails() {
  auto req = make_valid_request();
  RuntimeBudget budget;
  budget.max_replan_count = 0;
  req.runtime_budget = budget;
  auto result = validate_agent_request_field_rules(req);
  assert_true(!result.ok,
              "zero max_replan_count in budget should fail field rules");
}

// ===========================================================================
// Negative cases: inherited required-field failures (regression guard)
// ===========================================================================

// N17: Missing required field still rejected by field rules.
void test_missing_required_field_still_fails() {
  auto req = make_valid_request();
  req.request_id = std::nullopt;
  auto result = validate_agent_request_field_rules(req);
  assert_true(!result.ok,
              "missing request_id should still fail via field rules");
}

}  // namespace

int main() {
  try {
    // Positive cases (4)
    test_minimal_valid_request_passes_field_rules();
    test_full_valid_request_passes_field_rules();
    test_partial_optional_fields_passes();
    test_partial_runtime_budget_passes();

    // Negative cases: empty optional strings (7)
    test_empty_goal_hint_fails();
    test_empty_domain_context_fails();
    test_empty_constraint_set_fails();
    test_empty_approval_policy_hint_fails();
    test_empty_idempotency_key_fails();
    test_empty_locale_fails();
    test_empty_client_capabilities_fails();

    // Negative cases: zero numeric optionals (2)
    test_zero_timeout_ms_fails();
    test_zero_priority_fails();

    // Negative cases: tags violations (2)
    test_empty_tags_vector_fails();
    test_tags_with_empty_string_fails();

    // Negative cases: budget dimension violations (5)
    test_zero_budget_max_tokens_fails();
    test_zero_budget_max_turns_fails();
    test_zero_budget_max_tool_calls_fails();
    test_zero_budget_max_latency_ms_fails();
    test_zero_budget_max_replan_count_fails();

    // Negative cases: inherited required-field failure (1)
    test_missing_required_field_still_fails();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
