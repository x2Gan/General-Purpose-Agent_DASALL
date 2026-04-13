#include <algorithm>
#include <exception>
#include <iostream>
#include <string>

#include "support/TestAssertions.h"

#include "../../../llm/src/execution/ResponseNormalizer.h"

namespace {

using dasall::contracts::LLMResponse;
using dasall::contracts::LLMResponseKind;
using dasall::contracts::ResultCode;
using dasall::llm::AdapterCallResult;
using dasall::llm::execution::ResponseNormalizer;
using dasall::llm::execution::ResponseNormalizerContext;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

ResponseNormalizerContext make_context() {
  return ResponseNormalizerContext{
      .route_key = "deepseek-prod/deepseek-reasoner",
      .provider_id = "deepseek-prod",
      .model_id = "deepseek-reasoner",
      .model_name = "deepseek-reasoner",
      .prompt_id = "prompt.planner.reasoning",
      .prompt_version = "2026-04-13.1",
      .request_id = "req-normalizer-002",
      .llm_call_id = "call-normalizer-002",
      .completed_at_ms = 1710001002000,
  };
}

AdapterCallResult make_result() {
  LLMResponse response;
  response.request_id = "req-normalizer-002";
  response.llm_call_id = "call-normalizer-002";
  response.response_kind = LLMResponseKind::DirectResponse;
  response.content_payload = "The greater value is 9.11.";
  response.completed_at = 1710001002000;
  response.finish_reason = "stop";

  AdapterCallResult result;
  result.response = std::move(response);
  return result;
}

bool has_audit_prefix(const std::vector<std::string>& audit_events,
                      const std::string& prefix) {
  return std::find_if(audit_events.begin(), audit_events.end(), [&](const std::string& event) {
           return event.rfind(prefix, 0U) == 0U;
         }) != audit_events.end();
}

void test_reasoning_content_is_stripped_and_audited() {
  ResponseNormalizer normalizer;
  auto adapter_result = make_result();
  adapter_result.provider_diagnostics.reasoning_content = "compare decimal places step by step";
  adapter_result.provider_diagnostics.provider_trace_id = "trace-deepseek-001";

  const auto result = normalizer.normalize(adapter_result, make_context());

  assert_true(result.has_consistent_values() && result.succeeded(),
              "ResponseNormalizer should still succeed when provider-private reasoning_content is present");
  assert_true(result.reasoning_content_stripped,
              "ResponseNormalizer should explicitly mark reasoning_content as stripped");
  assert_true(has_audit_prefix(result.audit_events, "reasoning_content_stripped"),
              "ResponseNormalizer should emit an audit event when it strips reasoning_content");
  assert_equal(std::string("trace-deepseek-001"), result.provider_trace_id,
               "ResponseNormalizer should keep provider trace id inside module-local diagnostics only");
  assert_true(result.response->content_payload->find("decimal places") == std::string::npos,
              "ResponseNormalizer should not leak reasoning_content into shared assistant content");
}

void test_malformed_payload_fails_closed_as_provider_protocol() {
  ResponseNormalizer normalizer;
  auto adapter_result = make_result();
  adapter_result.response->content_payload = std::nullopt;

  const auto result = normalizer.normalize(adapter_result, make_context());

  assert_true(result.has_consistent_values() && !result.succeeded(),
              "ResponseNormalizer should fail closed when required response fields are missing");
  assert_true(result.result_code.has_value() &&
                  *result.result_code == ResultCode::ValidationFieldMissing,
              "ResponseNormalizer should surface malformed payloads through ValidationFieldMissing");
  assert_true(result.error.has_value() &&
                  result.error->details.message.find("response normalizer rejected adapter payload") != std::string::npos,
              "ResponseNormalizer should explain malformed payload failures with a protocol-level message");
  assert_true(has_audit_prefix(result.audit_events, "malformed_payload:"),
              "ResponseNormalizer should audit malformed payload rejections");
}

void test_unknown_finish_reason_is_normalized_and_audited() {
  ResponseNormalizer normalizer;
  auto adapter_result = make_result();
  adapter_result.response->finish_reason = "vendor_magic_stop";

  const auto result = normalizer.normalize(adapter_result, make_context());

  assert_true(result.has_consistent_values() && result.succeeded(),
              "ResponseNormalizer should keep successful payloads even when finish_reason is provider-specific");
  assert_equal(std::string("unknown"), *result.response->finish_reason,
               "ResponseNormalizer should canonicalize unsupported finish_reason values to unknown");
  assert_true(has_audit_prefix(result.audit_events, "unknown_finish_reason:"),
              "ResponseNormalizer should emit an audit record for unknown finish_reason values");
}

}  // namespace

int main() {
  try {
    test_reasoning_content_is_stripped_and_audited();
    test_malformed_payload_fails_closed_as_provider_protocol();
    test_unknown_finish_reason_is_normalized_and_audited();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}