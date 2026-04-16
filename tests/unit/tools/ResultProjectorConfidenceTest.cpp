#include <algorithm>
#include <exception>
#include <iostream>
#include <string>

#include "observation/ObservationDigestGuards.h"
#include "projection/ResultProjector.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] dasall::tools::route::ToolRouteDecision make_builtin_route() {
  return dasall::tools::route::ToolRouteDecision{
      .available = true,
      .route = dasall::contracts::ToolIRRoute::LocalTool,
      .lane_key = std::string("builtin"),
      .reason_code = std::string("route.builtin.selected"),
      .uses_stale_snapshot = false,
      .server_id = std::nullopt,
  };
}

[[nodiscard]] dasall::tools::ToolInvocationContext make_context() {
  return dasall::tools::ToolInvocationContext{
      .caller_domain = std::string("runtime.main"),
      .session_id = std::string("session-projector-failure"),
      .profile_snapshot = nullptr,
      .trace = {
          .trace_id = std::string("trace-projector-failure"),
          .span_id = std::string("span-projector-failure"),
          .parent_span_id = std::nullopt,
      },
      .confirmation_facts = std::nullopt,
  };
}

void test_result_projector_failure_digest_hides_raw_payload_and_remains_contract_valid() {
  const dasall::contracts::ToolResult result{
      .request_id = std::string("req-projector-failure"),
      .tool_call_id = std::string("call-projector-failure"),
      .tool_name = std::string("agent.terminal"),
      .success = false,
      .payload = std::string("{\"secret\":\"token-123\"}"),
      .error = dasall::contracts::ErrorInfo{
          .failure_type = dasall::contracts::ResultCodeCategory::Provider,
          .retryable = true,
          .safe_to_replan = false,
          .details = {
              .code = static_cast<int>(dasall::contracts::ResultCode::ProviderTimeout),
              .message = std::string("provider.timeout"),
              .stage = std::string("service.execute"),
          },
          .source_ref = {
              .ref_type = std::string("adapter_receipt"),
              .ref_id = std::string("receipt-projector-failure"),
          },
      },
      .side_effects = std::nullopt,
      .completed_at = 2800,
      .duration_ms = 40,
      .goal_id = std::string("goal-projector-failure"),
      .worker_task_id = std::string("worker-projector-failure"),
      .tags = std::vector<std::string>{"builtin"},
  };

  const dasall::tools::projection::ResultProjector projector;
  const auto envelope = projector.project_failure(result, make_builtin_route(), make_context());

  assert_true(envelope.observation_digest.has_value(),
              "failure projection should still emit digest");
  const auto& digest = *envelope.observation_digest;
  const auto required = dasall::contracts::validate_observation_digest_required_fields(digest);
  const auto boundary = dasall::contracts::validate_observation_digest_boundary(digest);
  assert_true(required.ok,
              "projected failure digest should satisfy required ObservationDigest fields");
  assert_true(boundary.ok,
              "projected failure digest should satisfy boundary validation");
  assert_true(digest.summary.has_value() &&
                  digest.summary->find("provider.timeout") != std::string::npos,
              "failure digest summary should come from ErrorInfo message");
  assert_true(digest.summary->find("token-123") == std::string::npos,
              "failure digest summary must not leak raw payload content");
  assert_true(digest.key_facts.has_value() &&
                  std::find(digest.key_facts->begin(),
                            digest.key_facts->end(),
                            std::string("failure_type=provider")) != digest.key_facts->end(),
              "failure digest should keep error category as explicit fact");
  assert_true(std::find_if(
                  digest.key_facts->begin(),
                  digest.key_facts->end(),
                  [](const std::string& fact) {
                    return fact.find("token-123") != std::string::npos;
                  }) == digest.key_facts->end(),
              "failure digest facts must not echo raw payload secrets");
  assert_true(digest.omitted_details.has_value() &&
                  std::find(digest.omitted_details->begin(),
                            digest.omitted_details->end(),
                            std::string("failure payload suppressed")) !=
                      digest.omitted_details->end(),
              "failure digest should explicitly record payload suppression");
  assert_equal(1.0f,
               digest.confidence.value_or(0.0f),
               "failure digest without truncation should retain full fidelity confidence");
}

}  // namespace

int main() {
  try {
    test_result_projector_failure_digest_hides_raw_payload_and_remains_contract_valid();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}