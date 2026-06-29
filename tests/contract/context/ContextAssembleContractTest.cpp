#include <exception>
#include <iostream>

#include "context/ContextAssembleRequest.h"
#include "context/ContextAssembleResult.h"
#include "support/TestAssertions.h"

namespace {

using dasall::contracts::ContextAssembleRequest;
using dasall::contracts::ContextAssembleResult;
using dasall::contracts::RetrievalEvidenceRef;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

ContextAssembleRequest make_valid_request() {
  return ContextAssembleRequest{
      .request_id = "request-001",
      .session_id = "session-001",
      .trace_id = "trace-001",
      .stage = "plan",
      .user_turn = "Summarize the latest deployment incident.",
      .goal_summary = "Produce a grounded response.",
      .constraints_summary = "Stay within the active runtime budget.",
      .latest_observation_digest_summary = "No prior observation.",
      .visible_tools = {"shell", "search"},
      .token_budget_hint = 6144,
      .latency_budget_ms = 250,
      .external_evidence = {"profile:desktop_full"},
      .retrieval_evidence_refs = {RetrievalEvidenceRef{
          .evidence_ref = "evidence-ref-001",
          .source_ref = "doc:memory-eval",
          .source_kind = "file",
          .summary_text = "Memory evaluation summary",
          .trust_level = "trusted",
          .freshness = "Fresh",
          .anchor_locator = "section:wp-mem-gap-016",
      }},
  };
}

void test_p1_request_preserves_runtime_projection_fields() {
  const auto request = make_valid_request();

  assert_equal("request-001",
               request.request_id,
               "P1: context assemble request must preserve the runtime request id");
  assert_equal("plan",
               request.stage,
               "P1: context assemble request must preserve the orchestration stage");
  assert_equal(6144,
               request.token_budget_hint,
               "P1: context assemble request must preserve the explicit token budget");
  assert_true(request.visible_tools.size() == 2U,
              "P1: context assemble request must preserve visible tool identifiers");
  assert_true(request.retrieval_evidence_refs.size() == 1U,
              "P1: context assemble request must preserve structured evidence refs");
}

void test_p2_result_preserves_context_packet_payload() {
  ContextAssembleResult result;
  result.context_packet.request_id = "request-001";
  result.context_packet.current_goal_summary = "Produce a grounded response.";
  result.compression_notes = {"summary_applied"};
  result.warnings = {"vector_unavailable"};
  result.degraded = true;

  assert_true(!result.result_code.has_value(),
              "P2: success-path context assemble result may leave result_code empty");
  assert_true(result.context_packet.request_id.has_value(),
              "P2: context assemble result must preserve the context packet payload");
  assert_equal("Produce a grounded response.",
               result.context_packet.current_goal_summary.value_or(std::string{}),
               "P2: context assemble result must preserve the projected goal summary");
  assert_true(result.degraded,
              "P2: context assemble result must preserve degraded flags when set");
}

void test_n1_default_constructed_request_starts_unprojected() {
  const ContextAssembleRequest request{};

  assert_true(request.request_id.empty(),
              "N1: default context assemble request must start without a runtime request id");
  assert_true(request.stage.empty(),
              "N1: default context assemble request must start without an orchestration stage");
  assert_equal(4096,
               request.token_budget_hint,
               "N1: default context assemble request must retain the documented token default");
  assert_true(request.retrieval_evidence_refs.empty(),
              "N1: default context assemble request must not synthesize retrieval refs");
}

void test_n2_default_constructed_result_starts_clean() {
  const ContextAssembleResult result{};

  assert_true(!result.result_code.has_value(),
              "N2: default context assemble result must not report a failure code");
  assert_true(result.warnings.empty(),
              "N2: default context assemble result must not synthesize warnings");
  assert_true(!result.degraded,
              "N2: default context assemble result must not start degraded");
}

int run_all_tests() {
  int passed = 0;
  int failed = 0;

  auto run = [&](void (*fn)(), const char* name) {
    try {
      fn();
      ++passed;
    } catch (const std::exception& ex) {
      std::cerr << "FAIL [" << name << "]: " << ex.what() << "\n";
      ++failed;
    }
  };

  run(test_p1_request_preserves_runtime_projection_fields,
      "P1_request_preserves_runtime_projection_fields");
  run(test_p2_result_preserves_context_packet_payload,
      "P2_result_preserves_context_packet_payload");
  run(test_n1_default_constructed_request_starts_unprojected,
      "N1_default_constructed_request_starts_unprojected");
  run(test_n2_default_constructed_result_starts_clean,
      "N2_default_constructed_result_starts_clean");

  std::cout << "ContextAssembleContractTest: " << passed << " passed, "
            << failed << " failed\n";
  return failed == 0 ? 0 : 1;
}

}  // namespace

int main() {
  return run_all_tests();
}