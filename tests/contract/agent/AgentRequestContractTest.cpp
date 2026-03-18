#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "agent/AgentRequest.h"
#include "agent/AgentRequestGuards.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

using dasall::contracts::AgentRequest;
using dasall::contracts::AgentRequestGuardResult;
using dasall::contracts::RequestChannel;
using dasall::contracts::validate_agent_request_boundary;
using dasall::contracts::validate_agent_request_required_fields;
using dasall::tests::support::assert_true;

// ---------------------------------------------------------------------------
// Helper: construct a valid minimal AgentRequest with all required fields set.
// ---------------------------------------------------------------------------

AgentRequest make_valid_request() {
  AgentRequest req;
  req.request_id = "req-001";
  req.session_id = "sess-001";
  req.trace_id = "trace-001";
  req.user_input = "Hello, agent";
  req.request_channel = RequestChannel::Cli;
  req.created_at = 1710000000000;
  return req;
}

// ===========================================================================
// Positive cases
// ===========================================================================

void test_valid_request_passes_required_fields_guard() {
  auto req = make_valid_request();
  auto result = validate_agent_request_required_fields(req);
  assert_true(result.ok,
              "valid request should pass required fields guard");
}

void test_valid_request_passes_boundary_guard() {
  auto req = make_valid_request();
  auto result = validate_agent_request_boundary(req);
  assert_true(result.ok,
              "valid request should pass boundary guard");
}

void test_valid_request_with_optional_fields_passes() {
  auto req = make_valid_request();
  req.goal_hint = "Summarize the document";
  req.domain_context = "engineering meeting notes";
  req.constraint_set = "no-external-calls";
  req.approval_policy_hint = "auto";
  req.locale = "zh-CN";
  req.client_capabilities = "streaming";
  req.idempotency_key = "idem-key-001";
  req.priority = 5;
  req.timeout_ms = 30000;
  req.deadline_at = 1710000030000;
  req.tags = std::vector<std::string>{"urgent", "review"};
  auto result = validate_agent_request_boundary(req);
  assert_true(result.ok,
              "valid request with all optional fields should pass");
}

void test_all_request_channels_accepted() {
  auto req = make_valid_request();

  req.request_channel = RequestChannel::Cli;
  assert_true(validate_agent_request_boundary(req).ok,
              "Cli channel should be accepted");

  req.request_channel = RequestChannel::Gateway;
  assert_true(validate_agent_request_boundary(req).ok,
              "Gateway channel should be accepted");

  req.request_channel = RequestChannel::Daemon;
  assert_true(validate_agent_request_boundary(req).ok,
              "Daemon channel should be accepted");

  req.request_channel = RequestChannel::Simulator;
  assert_true(validate_agent_request_boundary(req).ok,
              "Simulator channel should be accepted");
}

// ===========================================================================
// Negative cases: missing required fields
// ===========================================================================

void test_missing_request_id_fails() {
  auto req = make_valid_request();
  req.request_id = std::nullopt;
  auto result = validate_agent_request_required_fields(req);
  assert_true(!result.ok, "missing request_id should fail");
}

void test_empty_request_id_fails() {
  auto req = make_valid_request();
  req.request_id = "";
  auto result = validate_agent_request_required_fields(req);
  assert_true(!result.ok, "empty request_id should fail");
}

void test_missing_session_id_fails() {
  auto req = make_valid_request();
  req.session_id = std::nullopt;
  auto result = validate_agent_request_required_fields(req);
  assert_true(!result.ok, "missing session_id should fail");
}

void test_missing_trace_id_fails() {
  auto req = make_valid_request();
  req.trace_id = std::nullopt;
  auto result = validate_agent_request_required_fields(req);
  assert_true(!result.ok, "missing trace_id should fail");
}

void test_missing_user_input_fails() {
  auto req = make_valid_request();
  req.user_input = std::nullopt;
  auto result = validate_agent_request_required_fields(req);
  assert_true(!result.ok, "missing user_input should fail");
}

void test_empty_user_input_fails() {
  auto req = make_valid_request();
  req.user_input = "";
  auto result = validate_agent_request_required_fields(req);
  assert_true(!result.ok, "empty user_input should fail");
}

void test_unspecified_channel_fails() {
  auto req = make_valid_request();
  req.request_channel = RequestChannel::Unspecified;
  auto result = validate_agent_request_required_fields(req);
  assert_true(!result.ok, "Unspecified request_channel should fail");
}

void test_missing_channel_fails() {
  auto req = make_valid_request();
  req.request_channel = std::nullopt;
  auto result = validate_agent_request_required_fields(req);
  assert_true(!result.ok, "missing request_channel should fail");
}

void test_missing_created_at_fails() {
  auto req = make_valid_request();
  req.created_at = std::nullopt;
  auto result = validate_agent_request_required_fields(req);
  assert_true(!result.ok, "missing created_at should fail");
}

void test_zero_created_at_fails() {
  auto req = make_valid_request();
  req.created_at = 0;
  auto result = validate_agent_request_required_fields(req);
  assert_true(!result.ok, "zero created_at should fail");
}

void test_negative_created_at_fails() {
  auto req = make_valid_request();
  req.created_at = -1;
  auto result = validate_agent_request_required_fields(req);
  assert_true(!result.ok, "negative created_at should fail");
}

// ===========================================================================
// Negative cases: boundary violations
// ===========================================================================

void test_deadline_before_created_at_fails() {
  auto req = make_valid_request();
  req.deadline_at = 1709999999999;  // earlier than created_at
  auto result = validate_agent_request_boundary(req);
  assert_true(!result.ok, "deadline_at before created_at should fail");
}

void test_negative_deadline_fails() {
  auto req = make_valid_request();
  req.deadline_at = -1;
  auto result = validate_agent_request_boundary(req);
  assert_true(!result.ok, "negative deadline_at should fail");
}

}  // namespace

int main() {
  try {
    // Positive cases
    test_valid_request_passes_required_fields_guard();
    test_valid_request_passes_boundary_guard();
    test_valid_request_with_optional_fields_passes();
    test_all_request_channels_accepted();

    // Negative cases: missing required fields
    test_missing_request_id_fails();
    test_empty_request_id_fails();
    test_missing_session_id_fails();
    test_missing_trace_id_fails();
    test_missing_user_input_fails();
    test_empty_user_input_fails();
    test_unspecified_channel_fails();
    test_missing_channel_fails();
    test_missing_created_at_fails();
    test_zero_created_at_fails();
    test_negative_created_at_fails();

    // Negative cases: boundary violations
    test_deadline_before_created_at_fails();
    test_negative_deadline_fails();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
