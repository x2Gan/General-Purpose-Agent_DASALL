// ==========================================================================
// PromptComposeRequestContractTest.cpp
//
// WP04-T002-B: Contract test for PromptComposeRequest.h and
// PromptComposeRequestGuards.h.
//
// Validates the WP04-T002 boundary and field rules for PromptComposeRequest:
//   - Three-layer guards (L1 required / L2 boundary / L3 field rules)
//     enforce all WP04-T002-D constraints.
//   - Forbidden context-ownership field names are rejected by
//     PromptBoundaryContracts.h (T001-B), verified via cross-check below.
//
// Test coverage (positive + negative):
//
//   Positive (4 cases):
//     P1 — Minimal valid request (4 required fields) passes L1 guard.
//     P2 — Valid request passes L2 boundary guard.
//     P3 — Valid request with all optional fields passes L3 field rules.
//     P4 — Each CompositionStage enum value (Planning through Response)
//          is accepted by L2 boundary guard.
//
//   Negative (13 cases):
//     N1  — Missing request_id is rejected at L1.
//     N2  — Missing stage is rejected at L1.
//     N3  — Unspecified stage is rejected at L1.
//     N4  — Missing context_packet_id is rejected at L1.
//     N5  — Missing created_at is rejected at L1.
//     N6  — Zero created_at is rejected at L1.
//     N7  — Out-of-range stage integer is rejected at L2.
//     N8  — Empty task_type (present-but-empty) is rejected at L3.
//     N9  — Empty prompt_release_id is rejected at L3.
//     N10 — Empty model_route is rejected at L3.
//     N11 — Empty visible_tools vector is rejected at L3.
//     N12 — visible_tools containing an empty-string element is rejected at L3.
//     N13 — Empty tags vector is rejected at L3.
//
// Verification command (WP04-T002):
//   cmake --build build-ci --target dasall_contract_tests && \
//   ctest --test-dir build-ci -R PromptComposeRequestContractTest --output-on-failure
// ==========================================================================
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "prompt/PromptBoundaryContracts.h"
#include "prompt/PromptComposeRequest.h"
#include "prompt/PromptComposeRequestGuards.h"
#include "support/TestAssertions.h"

namespace {

using dasall::contracts::CompositionStage;
using dasall::contracts::PromptComposeRequest;
using dasall::contracts::PromptComposeRequestGuardResult;
using dasall::contracts::validate_prompt_compose_request_required_fields;
using dasall::contracts::validate_prompt_compose_request_boundary;
using dasall::contracts::validate_prompt_compose_request_field_rules;
using dasall::contracts::evaluate_compose_request_field_boundary;

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

// ---------------------------------------------------------------------------
// Helper: build a valid minimal PromptComposeRequest with all required fields.
// ---------------------------------------------------------------------------
PromptComposeRequest make_valid_request() {
  PromptComposeRequest req;
  req.request_id       = "req-002";
  req.stage            = CompositionStage::Planning;
  req.context_packet_id = "req-002";  // Equals request_id (WP02-T009 chain)
  req.created_at       = 1710000100000;
  return req;
}

// ---------------------------------------------------------------------------
// Helper: build a valid request with all optional fields populated.
// ---------------------------------------------------------------------------
PromptComposeRequest make_full_request() {
  auto req              = make_valid_request();
  req.task_type         = "summarize";
  req.prompt_release_id = "prompt-release-v1.2.0";
  req.visible_tools     = std::vector<std::string>{"ci_query_tool", "log_search_tool"};
  req.model_route       = "gpt-4o-mini";
  req.output_schema_ref = "schema:bullet-summary-v1";
  req.response_format   = "json_object";
  req.tags              = std::vector<std::string>{"planning", "sprint-42"};
  return req;
}

// ===========================================================================
// Positive cases
// ===========================================================================

// P1: Minimal valid request passes Layer 1 (required-field guard).
void test_valid_minimal_request_passes_required_fields() {
  const auto req    = make_valid_request();
  const auto result = validate_prompt_compose_request_required_fields(req);

  assert_true(result.ok,
              "P1: minimal valid request must pass required-field guard");
}

// P2: Minimal valid request passes Layer 2 (boundary guard).
void test_valid_minimal_request_passes_boundary() {
  const auto req    = make_valid_request();
  const auto result = validate_prompt_compose_request_boundary(req);

  assert_true(result.ok,
              "P2: minimal valid request must pass boundary guard");
}

// P3: Full valid request passes Layer 3 (field-rules guard).
void test_valid_full_request_passes_field_rules() {
  const auto req    = make_full_request();
  const auto result = validate_prompt_compose_request_field_rules(req);

  assert_true(result.ok,
              "P3: full valid request must pass field-rules guard");
}

// P4: All four CompositionStage enum values (Planning through Response)
//     are accepted by the boundary guard.
void test_all_stages_accepted_by_boundary_guard() {
  auto req = make_valid_request();

  req.stage = CompositionStage::Planning;
  assert_true(validate_prompt_compose_request_boundary(req).ok,
              "P4: Planning stage must be accepted");

  req.stage = CompositionStage::Execution;
  assert_true(validate_prompt_compose_request_boundary(req).ok,
              "P4: Execution stage must be accepted");

  req.stage = CompositionStage::Reflection;
  assert_true(validate_prompt_compose_request_boundary(req).ok,
              "P4: Reflection stage must be accepted");

  req.stage = CompositionStage::Response;
  assert_true(validate_prompt_compose_request_boundary(req).ok,
              "P4: Response stage must be accepted");
}

// ===========================================================================
// Negative cases — Layer 1: required-field presence
// ===========================================================================

// N1: Missing request_id is rejected at Layer 1.
void test_missing_request_id_rejected() {
  auto req      = make_valid_request();
  req.request_id = std::nullopt;
  const auto result = validate_prompt_compose_request_required_fields(req);

  assert_true(!result.ok,
              "N1: missing request_id must be rejected");
  assert_equal("request_id is required and must be non-empty",
               std::string(result.reason),
               "N1: rejection reason must identify request_id");
}

// N2: Missing stage (nullopt) is rejected at Layer 1.
void test_missing_stage_rejected() {
  auto req  = make_valid_request();
  req.stage = std::nullopt;
  const auto result = validate_prompt_compose_request_required_fields(req);

  assert_true(!result.ok,
              "N2: missing stage must be rejected");
  assert_equal("stage is required and must not be Unspecified",
               std::string(result.reason),
               "N2: rejection reason must identify stage");
}

// N3: Unspecified stage (value 0) is rejected at Layer 1.
void test_unspecified_stage_rejected() {
  auto req  = make_valid_request();
  req.stage = CompositionStage::Unspecified;
  const auto result = validate_prompt_compose_request_required_fields(req);

  assert_true(!result.ok,
              "N3: Unspecified stage must be rejected");
}

// N4: Missing context_packet_id is rejected at Layer 1.
void test_missing_context_packet_id_rejected() {
  auto req              = make_valid_request();
  req.context_packet_id = std::nullopt;
  const auto result = validate_prompt_compose_request_required_fields(req);

  assert_true(!result.ok,
              "N4: missing context_packet_id must be rejected");
  assert_equal("context_packet_id is required and must be non-empty",
               std::string(result.reason),
               "N4: rejection reason must identify context_packet_id");
}

// N5: Missing created_at is rejected at Layer 1.
void test_missing_created_at_rejected() {
  auto req       = make_valid_request();
  req.created_at = std::nullopt;
  const auto result = validate_prompt_compose_request_required_fields(req);

  assert_true(!result.ok,
              "N5: missing created_at must be rejected");
}

// N6: Zero created_at is rejected at Layer 1.
void test_zero_created_at_rejected() {
  auto req       = make_valid_request();
  req.created_at = 0;
  const auto result = validate_prompt_compose_request_required_fields(req);

  assert_true(!result.ok,
              "N6: created_at == 0 must be rejected (must be positive)");
}

// ===========================================================================
// Negative cases — Layer 2: boundary constraints
// ===========================================================================

// N7: Out-of-range stage (value 99) is rejected at Layer 2.
void test_out_of_range_stage_rejected() {
  auto req  = make_valid_request();
  // Cast an out-of-range integer directly to bypass the enum type.
  req.stage = static_cast<CompositionStage>(99);
  const auto result = validate_prompt_compose_request_boundary(req);

  assert_true(!result.ok,
              "N7: out-of-range stage value must be rejected at boundary guard");
  assert_equal("stage value is outside the known CompositionStage enum range",
               std::string(result.reason),
               "N7: rejection reason must identify stage range violation");
}

// ===========================================================================
// Negative cases — Layer 3: optional-field rules
// ===========================================================================

// N8: Present-but-empty task_type is rejected at Layer 3.
void test_empty_task_type_rejected() {
  auto req      = make_valid_request();
  req.task_type = "";
  const auto result = validate_prompt_compose_request_field_rules(req);

  assert_true(!result.ok,
              "N8: empty task_type must be rejected");
  assert_equal("task_type must be non-empty when present",
               std::string(result.reason),
               "N8: rejection reason must identify task_type");
}

// N9: Present-but-empty prompt_release_id is rejected at Layer 3.
void test_empty_prompt_release_id_rejected() {
  auto req              = make_valid_request();
  req.prompt_release_id = "";
  const auto result = validate_prompt_compose_request_field_rules(req);

  assert_true(!result.ok,
              "N9: empty prompt_release_id must be rejected");
  assert_equal("prompt_release_id must be non-empty when present",
               std::string(result.reason),
               "N9: rejection reason must identify prompt_release_id");
}

// N10: Present-but-empty model_route is rejected at Layer 3.
void test_empty_model_route_rejected() {
  auto req        = make_valid_request();
  req.model_route = "";
  const auto result = validate_prompt_compose_request_field_rules(req);

  assert_true(!result.ok,
              "N10: empty model_route must be rejected");
  assert_equal("model_route must be non-empty when present",
               std::string(result.reason),
               "N10: rejection reason must identify model_route");
}

// N11: Empty visible_tools vector (present but size == 0) is rejected at L3.
void test_empty_visible_tools_vector_rejected() {
  auto req           = make_valid_request();
  req.visible_tools  = std::vector<std::string>{};
  const auto result = validate_prompt_compose_request_field_rules(req);

  assert_true(!result.ok,
              "N11: empty visible_tools vector must be rejected");
  assert_equal("visible_tools must contain at least one item when present",
               std::string(result.reason),
               "N11: rejection reason must identify visible_tools");
}

// N12: visible_tools containing an empty-string element is rejected at L3.
void test_visible_tools_with_empty_element_rejected() {
  auto req          = make_valid_request();
  req.visible_tools = std::vector<std::string>{"ci_query_tool", ""};
  const auto result = validate_prompt_compose_request_field_rules(req);

  assert_true(!result.ok,
              "N12: visible_tools with an empty element must be rejected");
  assert_equal("visible_tools must not contain empty-string elements",
               std::string(result.reason),
               "N12: rejection reason must identify visible_tools empty element");
}

// N13: Empty tags vector (present but size == 0) is rejected at L3.
void test_empty_tags_vector_rejected() {
  auto req  = make_valid_request();
  req.tags  = std::vector<std::string>{};
  const auto result = validate_prompt_compose_request_field_rules(req);

  assert_true(!result.ok,
              "N13: empty tags vector must be rejected");
  assert_equal("tags must contain at least one item when present",
               std::string(result.reason),
               "N13: rejection reason must identify tags");
}

// ===========================================================================
// Cross-check: PromptBoundaryContracts.h rejects context-ownership fields
// (WP04-T001-B verification from the T002 perspective)
// ===========================================================================
void test_context_ownership_fields_rejected_by_boundary_contracts() {
  // These field names are forbidden in PromptComposeRequest per
  // WP04-T001-D §3 / ADR-006 §6.2/§7.
  assert_true(
      !dasall::contracts::evaluate_compose_request_field_boundary("memory_snapshot").allowed,
      "context-check: memory_snapshot must be rejected in PromptComposeRequest");
  assert_true(
      !dasall::contracts::evaluate_compose_request_field_boundary("retrieval_candidates").allowed,
      "context-check: retrieval_candidates must be rejected in PromptComposeRequest");
  assert_true(
      !dasall::contracts::evaluate_compose_request_field_boundary("context_packet_internal").allowed,
      "context-check: context_packet_internal must be rejected in PromptComposeRequest");
  assert_true(
      !dasall::contracts::evaluate_compose_request_field_boundary("knowledge_fragments").allowed,
      "context-check: knowledge_fragments must be rejected in PromptComposeRequest");
}

}  // namespace

int main() {
  try {
    // Positive cases
    test_valid_minimal_request_passes_required_fields();
    test_valid_minimal_request_passes_boundary();
    test_valid_full_request_passes_field_rules();
    test_all_stages_accepted_by_boundary_guard();

    // Negative cases — Layer 1
    test_missing_request_id_rejected();
    test_missing_stage_rejected();
    test_unspecified_stage_rejected();
    test_missing_context_packet_id_rejected();
    test_missing_created_at_rejected();
    test_zero_created_at_rejected();

    // Negative cases — Layer 2
    test_out_of_range_stage_rejected();

    // Negative cases — Layer 3
    test_empty_task_type_rejected();
    test_empty_prompt_release_id_rejected();
    test_empty_model_route_rejected();
    test_empty_visible_tools_vector_rejected();
    test_visible_tools_with_empty_element_rejected();
    test_empty_tags_vector_rejected();

    // Cross-check: boundary contracts
    test_context_ownership_fields_rejected_by_boundary_contracts();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
