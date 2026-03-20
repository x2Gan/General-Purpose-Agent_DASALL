#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "checkpoint/RuntimeBudget.h"
#include "dasall/tests/support/TestAssertions.h"
#include "llm/LLMBoundaryGuards.h"
#include "llm/LLMRequest.h"
#include "llm/LLMResponse.h"

namespace {

using dasall::contracts::LLMGuardResult;
using dasall::contracts::LLMRequest;
using dasall::contracts::LLMRequestBoundaryDecision;
using dasall::contracts::LLMRequestMode;
using dasall::contracts::LLMResponse;
using dasall::contracts::LLMResponseBoundaryDecision;
using dasall::contracts::LLMResponseKind;
using dasall::contracts::RuntimeBudget;
using dasall::contracts::evaluate_llm_request_field_boundary;
using dasall::contracts::evaluate_llm_response_field_boundary;
using dasall::contracts::validate_llm_request_boundary;
using dasall::contracts::validate_llm_request_field_rules;
using dasall::contracts::validate_llm_request_required_fields;
using dasall::contracts::validate_llm_response_boundary;
using dasall::contracts::validate_llm_response_field_rules;
using dasall::contracts::validate_llm_response_required_fields;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

// Builds the minimal valid LLMRequest accepted by the required-field guards.
LLMRequest make_valid_request() {
  LLMRequest request;
  request.request_id = "req-005-010";
  request.llm_call_id = "llm-call-001";
  request.model_route = "planner.cloud.primary";
  request.request_mode = LLMRequestMode::Unary;
  request.messages = std::vector<std::string>{"system:plan carefully", "user:help me"};
  request.created_at = 1710001000000;
  return request;
}

// Builds the full valid LLMRequest used by the field-rule tests.
LLMRequest make_full_request() {
  auto request = make_valid_request();
  request.prompt_id = "prompt.plan.default";
  request.prompt_version = "2026-03-20.1";
  request.output_schema_ref = "schema://llm/plan/v1";
  request.response_format = "json_schema";
  request.runtime_budget = RuntimeBudget{
      .max_tokens = 2048,
      .max_turns = 3,
      .max_tool_calls = 2,
      .max_latency_ms = 8000,
      .max_replan_count = 1,
  };
  request.max_output_tokens = 512;
  request.timeout_ms = 4000;
  request.tags = std::vector<std::string>{"llm", "contract"};
  return request;
}

// Builds the minimal valid LLMResponse accepted by the required-field guards.
LLMResponse make_valid_response() {
  LLMResponse response;
  response.request_id = "req-005-010";
  response.llm_call_id = "llm-call-001";
  response.response_kind = LLMResponseKind::DirectResponse;
  response.content_payload = R"({"answer":"ok"})";
  response.completed_at = 1710001001000;
  return response;
}

// Builds the full valid LLMResponse used by the field-rule tests.
LLMResponse make_full_response() {
  auto response = make_valid_response();
  response.model_name = "gpt-5.4";
  response.prompt_id = "prompt.plan.default";
  response.prompt_version = "2026-03-20.1";
  response.finish_reason = "stop";
  response.input_tokens = 120;
  response.output_tokens = 48;
  response.total_tokens = 168;
  response.tags = std::vector<std::string>{"llm", "success"};
  return response;
}

// Verifies the minimal request satisfies required-field validation.
void test_valid_minimal_request_passes_required_fields() {
  const LLMGuardResult result =
      validate_llm_request_required_fields(make_valid_request());
  assert_true(result.ok,
              "minimal valid LLMRequest should pass required-field validation");
}

// Verifies the full request satisfies optional field rules.
void test_valid_full_request_passes_field_rules() {
  const LLMGuardResult result = validate_llm_request_field_rules(make_full_request());
  assert_true(result.ok,
              "full valid LLMRequest should pass field-rule validation");
}

// Verifies the minimal response satisfies boundary validation.
void test_valid_minimal_response_passes_boundary() {
  const LLMGuardResult result = validate_llm_response_boundary(make_valid_response());
  assert_true(result.ok,
              "minimal valid LLMResponse should pass boundary validation");
}

// Verifies the full response satisfies optional field rules.
void test_valid_full_response_passes_field_rules() {
  const LLMGuardResult result = validate_llm_response_field_rules(make_full_response());
  assert_true(result.ok,
              "full valid LLMResponse should pass field-rule validation");
}

// Verifies a missing request route is rejected because routing is part of the
// stable llm request handoff.
void test_missing_model_route_is_rejected() {
  auto request = make_valid_request();
  request.model_route = std::nullopt;
  const LLMGuardResult result = validate_llm_request_required_fields(request);

  assert_true(!result.ok, "missing model_route must be rejected");
  assert_equal("model_route is required and must be non-empty",
               std::string(result.reason),
               "missing model_route should report the canonical reason");
}

// Verifies prompt audit anchors remain paired and cannot drift.
void test_prompt_audit_fields_must_be_paired_in_request() {
  auto request = make_valid_request();
  request.prompt_id = "prompt.plan.default";
  const LLMGuardResult result = validate_llm_request_field_rules(request);

  assert_true(!result.ok,
              "prompt_id without prompt_version must be rejected");
  assert_equal(
      "prompt_id and prompt_version must either both be present or both be absent",
      std::string(result.reason),
      "unpaired prompt audit anchors should report the canonical reason");
}

// Verifies request tags remain non-duplicated audit labels.
void test_duplicate_request_tags_are_rejected() {
  auto request = make_valid_request();
  request.tags = std::vector<std::string>{"llm", "llm"};
  const LLMGuardResult result = validate_llm_request_field_rules(request);

  assert_true(!result.ok, "duplicate request tags must be rejected");
  assert_equal("tags must not contain duplicate items",
               std::string(result.reason),
               "duplicate request tags should report the canonical reason");
}

// Verifies request_mode rejects unknown enum values.
void test_out_of_range_request_mode_is_rejected() {
  auto request = make_valid_request();
  request.request_mode = static_cast<LLMRequestMode>(99);
  const LLMGuardResult result = validate_llm_request_boundary(request);

  assert_true(!result.ok, "out-of-range request_mode must be rejected");
  assert_equal("request_mode value is outside the known enum range",
               std::string(result.reason),
               "out-of-range request_mode should report the canonical reason");
}

// Verifies the request boundary rejects provider-private request fields.
void test_request_provider_private_field_is_rejected() {
  const auto result = evaluate_llm_request_field_boundary("provider_payload");

  assert_true(!result.allowed,
              "provider_payload must be rejected for LLMRequest");
  assert_equal(
      static_cast<int>(LLMRequestBoundaryDecision::RejectProviderPrivateField),
      static_cast<int>(result.decision),
      "provider_payload should map to RejectProviderPrivateField");
}

// Verifies the request boundary rejects prompt asset source fields.
void test_request_prompt_asset_field_is_rejected() {
  const auto result = evaluate_llm_request_field_boundary("task_template");

  assert_true(!result.allowed,
              "task_template must be rejected for LLMRequest");
  assert_equal(static_cast<int>(LLMRequestBoundaryDecision::RejectPromptAssetField),
               static_cast<int>(result.decision),
               "task_template should map to RejectPromptAssetField");
}

// Verifies a refusal response must carry a refusal reason.
void test_refusal_response_requires_refusal_reason() {
  auto response = make_valid_response();
  response.response_kind = LLMResponseKind::Refusal;
  const LLMGuardResult result = validate_llm_response_field_rules(response);

  assert_true(!result.ok,
              "Refusal response without refusal_reason must be rejected");
  assert_equal("refusal_reason is required when response_kind is Refusal",
               std::string(result.reason),
               "missing refusal_reason should report the canonical reason");
}

// Verifies refusal metadata does not leak into non-refusal branches.
void test_refusal_reason_must_be_absent_for_non_refusal() {
  auto response = make_valid_response();
  response.refusal_reason = "policy block";
  const LLMGuardResult result = validate_llm_response_field_rules(response);

  assert_true(!result.ok,
              "refusal_reason on a non-refusal response must be rejected");
  assert_equal(
      "refusal_reason must be absent unless response_kind is Refusal",
      std::string(result.reason),
      "unexpected refusal_reason should report the canonical reason");
}

// Verifies usage accounting remains internally consistent.
void test_response_usage_totals_must_match() {
  auto response = make_valid_response();
  response.input_tokens = 100;
  response.output_tokens = 20;
  response.total_tokens = 119;
  const LLMGuardResult result = validate_llm_response_field_rules(response);

  assert_true(!result.ok, "mismatched total_tokens must be rejected");
  assert_equal("total_tokens must equal input_tokens + output_tokens",
               std::string(result.reason),
               "usage mismatch should report the canonical reason");
}

// Verifies response tags remain non-duplicated audit labels.
void test_duplicate_response_tags_are_rejected() {
  auto response = make_valid_response();
  response.tags = std::vector<std::string>{"llm", "llm"};
  const LLMGuardResult result = validate_llm_response_field_rules(response);

  assert_true(!result.ok, "duplicate response tags must be rejected");
  assert_equal("tags must not contain duplicate items",
               std::string(result.reason),
               "duplicate response tags should report the canonical reason");
}

// Verifies response_kind rejects unknown enum values.
void test_out_of_range_response_kind_is_rejected() {
  auto response = make_valid_response();
  response.response_kind = static_cast<LLMResponseKind>(99);
  const LLMGuardResult result = validate_llm_response_boundary(response);

  assert_true(!result.ok, "out-of-range response_kind must be rejected");
  assert_equal("response_kind value is outside the known enum range",
               std::string(result.reason),
               "out-of-range response_kind should report the canonical reason");
}

// Verifies the response boundary rejects provider-private response fields.
void test_response_provider_private_field_is_rejected() {
  const auto result = evaluate_llm_response_field_boundary("raw_provider_response");

  assert_true(!result.allowed,
              "raw_provider_response must be rejected for LLMResponse");
  assert_equal(
      static_cast<int>(LLMResponseBoundaryDecision::RejectProviderPrivateField),
      static_cast<int>(result.decision),
      "raw_provider_response should map to RejectProviderPrivateField");
}

// Verifies the response boundary rejects execution control fields.
void test_response_execution_control_field_is_rejected() {
  const auto result = evaluate_llm_response_field_boundary("tool_request");

  assert_true(!result.allowed,
              "tool_request must be rejected for LLMResponse");
  assert_equal(
      static_cast<int>(LLMResponseBoundaryDecision::RejectExecutionControlField),
      static_cast<int>(result.decision),
      "tool_request should map to RejectExecutionControlField");
}

// Verifies ordinary stable field names remain allowed on both objects.
void test_regular_fields_remain_allowed() {
  const auto request_result = evaluate_llm_request_field_boundary("model_route");
  assert_true(request_result.allowed,
              "model_route should remain allowed in LLMRequest");

  const auto response_result = evaluate_llm_response_field_boundary("model_name");
  assert_true(response_result.allowed,
              "model_name should remain allowed in LLMResponse");
}

}  // namespace

int main() {
  try {
    test_valid_minimal_request_passes_required_fields();
    test_valid_full_request_passes_field_rules();
    test_valid_minimal_response_passes_boundary();
    test_valid_full_response_passes_field_rules();
    test_missing_model_route_is_rejected();
    test_prompt_audit_fields_must_be_paired_in_request();
    test_duplicate_request_tags_are_rejected();
    test_out_of_range_request_mode_is_rejected();
    test_request_provider_private_field_is_rejected();
    test_request_prompt_asset_field_is_rejected();
    test_refusal_response_requires_refusal_reason();
    test_refusal_reason_must_be_absent_for_non_refusal();
    test_response_usage_totals_must_match();
    test_duplicate_response_tags_are_rejected();
    test_out_of_range_response_kind_is_rejected();
    test_response_provider_private_field_is_rejected();
    test_response_execution_control_field_is_rejected();
    test_regular_fields_remain_allowed();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}