#include <exception>
#include <iostream>
#include <string>

#include "support/TestAssertions.h"

#include "../../../llm/src/execution/ResponseNormalizer.h"

namespace {

using dasall::contracts::LLMResponse;
using dasall::contracts::LLMResponseKind;
using dasall::contracts::PromptEvalStatus;
using dasall::llm::AdapterCallResult;
using dasall::llm::execution::ResponseNormalizer;
using dasall::llm::execution::ResponseNormalizerContext;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

ResponseNormalizerContext make_context() {
  return ResponseNormalizerContext{
      .route_key = "deepseek-prod/deepseek-chat",
      .provider_id = "deepseek-prod",
      .model_id = "deepseek-chat",
      .model_name = "deepseek-chat",
      .prompt_id = "prompt.planner.default",
      .prompt_version = "2026-04-13.1",
      .prompt_eval_status = PromptEvalStatus::Stable,
      .prompt_release_scope = "desktop_full",
      .request_id = "req-normalizer-001",
      .llm_call_id = "call-normalizer-001",
      .completed_at_ms = 1710001001000,
  };
}

AdapterCallResult make_result(LLMResponseKind kind,
                              std::string content,
                              std::string finish_reason = "stop",
                              std::string refusal_reason = {}) {
  LLMResponse response;
  response.request_id = "req-normalizer-001";
  response.llm_call_id = "call-normalizer-001";
  response.response_kind = kind;
  response.content_payload = std::move(content);
  response.completed_at = 1710001001000;
  response.finish_reason = std::move(finish_reason);
  if (!refusal_reason.empty()) {
    response.refusal_reason = std::move(refusal_reason);
  }

  AdapterCallResult result;
  result.response = std::move(response);
  return result;
}

void test_direct_response_mapping_preserves_content_and_enriches_metadata() {
  ResponseNormalizer normalizer;
  const auto result = normalizer.normalize(
      make_result(LLMResponseKind::DirectResponse, "final-answer"), make_context());

  assert_true(result.has_consistent_values() && result.succeeded(),
              "ResponseNormalizer should keep direct responses as successful normalized outputs");
  assert_true(result.response->response_kind.has_value() &&
                  *result.response->response_kind == LLMResponseKind::DirectResponse,
              "ResponseNormalizer should preserve the direct response semantic kind");
  assert_equal(std::string("final-answer"), *result.response->content_payload,
               "ResponseNormalizer should preserve the direct response content payload");
  assert_equal(std::string("prompt.planner.default"), *result.response->prompt_id,
               "ResponseNormalizer should enrich missing prompt_id from the manager context");
  assert_equal(std::string("2026-04-13.1"), *result.response->prompt_version,
               "ResponseNormalizer should enrich missing prompt_version from the manager context");
  assert_true(result.response->eval_status.has_value() &&
                  *result.response->eval_status == PromptEvalStatus::Stable,
              "ResponseNormalizer should enrich missing eval_status from the manager context");
  assert_equal(std::string("desktop_full"), *result.response->release_scope,
               "ResponseNormalizer should enrich missing release_scope from the manager context");
  assert_equal(std::string("deepseek-chat"), *result.response->model_name,
               "ResponseNormalizer should enrich missing model_name from the resolved route context");
}

void test_tool_call_intent_mapping_normalizes_finish_reason_aliases() {
  ResponseNormalizer normalizer;
  const auto result = normalizer.normalize(
      make_result(LLMResponseKind::ToolCallIntent,
                  R"({\"tool\":\"search\",\"arguments\":{}})",
                  "tool_calls"),
      make_context());

  assert_true(result.has_consistent_values() && result.succeeded(),
              "ResponseNormalizer should keep tool call intents as successful normalized outputs");
  assert_true(result.response->response_kind.has_value() &&
                  *result.response->response_kind == LLMResponseKind::ToolCallIntent,
              "ResponseNormalizer should preserve the tool call semantic branch");
  assert_equal(std::string("tool_call"), *result.response->finish_reason,
               "ResponseNormalizer should canonicalize provider finish_reason aliases to tool_call");
}

void test_clarification_mapping_preserves_semantic_branch() {
  ResponseNormalizer normalizer;
  const auto result = normalizer.normalize(
      make_result(LLMResponseKind::ClarificationRequest,
                  "Please clarify which subsystem log you want summarized.",
                  "clarification"),
      make_context());

  assert_true(result.has_consistent_values() && result.succeeded(),
              "ResponseNormalizer should keep clarification requests as successful normalized outputs");
  assert_true(result.response->response_kind.has_value() &&
                  *result.response->response_kind == LLMResponseKind::ClarificationRequest,
              "ResponseNormalizer should preserve the clarification semantic branch");
}

void test_replan_mapping_preserves_semantic_branch() {
  ResponseNormalizer normalizer;
  const auto result = normalizer.normalize(
      make_result(LLMResponseKind::ReplanSuggestion,
                  "Current plan lacks provider credentials; propose replan.",
                  "replan"),
      make_context());

  assert_true(result.has_consistent_values() && result.succeeded(),
              "ResponseNormalizer should keep replan suggestions as successful normalized outputs");
  assert_true(result.response->response_kind.has_value() &&
                  *result.response->response_kind == LLMResponseKind::ReplanSuggestion,
              "ResponseNormalizer should preserve the replan semantic branch");
}

void test_refusal_mapping_preserves_refusal_reason() {
  ResponseNormalizer normalizer;
  const auto result = normalizer.normalize(
      make_result(LLMResponseKind::Refusal,
                  "I cannot comply with that request.",
                  "refusal",
                  "policy_denied"),
      make_context());

  assert_true(result.has_consistent_values() && result.succeeded(),
              "ResponseNormalizer should keep refusals as successful normalized outputs");
  assert_true(result.response->response_kind.has_value() &&
                  *result.response->response_kind == LLMResponseKind::Refusal,
              "ResponseNormalizer should preserve the refusal semantic branch");
  assert_equal(std::string("policy_denied"), *result.response->refusal_reason,
               "ResponseNormalizer should preserve refusal_reason on refusal branches");
}

}  // namespace

int main() {
  try {
    test_direct_response_mapping_preserves_content_and_enriches_metadata();
    test_tool_call_intent_mapping_normalizes_finish_reason_aliases();
    test_clarification_mapping_preserves_semantic_branch();
    test_replan_mapping_preserves_semantic_branch();
    test_refusal_mapping_preserves_refusal_reason();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}