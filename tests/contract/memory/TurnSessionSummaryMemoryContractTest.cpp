#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "support/TestAssertions.h"
#include "memory/Session.h"
#include "memory/SummaryMemory.h"
#include "memory/Turn.h"

namespace {

using dasall::contracts::Session;
using dasall::contracts::SessionBoundaryDecision;
using dasall::contracts::SummaryMemory;
using dasall::contracts::SummaryMemoryBoundaryDecision;
using dasall::contracts::Turn;
using dasall::contracts::TurnBoundaryDecision;
using dasall::contracts::evaluate_session_field_boundary;
using dasall::contracts::evaluate_summary_memory_field_boundary;
using dasall::contracts::evaluate_turn_field_boundary;
using dasall::contracts::validate_session_field_rules;
using dasall::contracts::validate_session_required_fields;
using dasall::contracts::validate_summary_memory_field_rules;
using dasall::contracts::validate_summary_memory_required_fields;
using dasall::contracts::validate_turn_field_rules;
using dasall::contracts::validate_turn_required_fields;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

// Factory for a minimally valid Turn contract object.
Turn make_valid_turn() {
  Turn turn;
  turn.turn_id = "turn-006-001";
  turn.session_id = "session-006-001";
  turn.user_input = "请总结本轮工具调用结果。";
  turn.created_at = 1710000300000;
  return turn;
}

// Factory for a fully populated Turn contract object.
Turn make_full_turn() {
  auto turn = make_valid_turn();
  turn.agent_response = "已完成本轮总结。";
  turn.tool_call_refs = std::vector<std::string>{"tool-call-1", "tool-call-2"};
  turn.observation_refs = std::vector<std::string>{"obs-1", "obs-2"};
  turn.summary_memory_ref = "summary-006-001";
  turn.tags = std::vector<std::string>{"memory", "turn"};
  return turn;
}

// Factory for a minimally valid Session contract object.
Session make_valid_session() {
  Session session;
  session.session_id = "session-006-001";
  session.turn_ids = std::vector<std::string>{"turn-006-001"};
  session.created_at = 1710000300000;
  return session;
}

// Factory for a fully populated Session contract object.
Session make_full_session() {
  auto session = make_valid_session();
  session.user_id = "user-006-001";
  session.latest_summary_memory_ref = "summary-006-001";
  session.metadata_digest = R"({"profile":"default","state":"active"})";
  session.last_active_at = 1710000305000;
  session.tags = std::vector<std::string>{"memory", "session"};
  return session;
}

// Factory for a minimally valid SummaryMemory contract object.
SummaryMemory make_valid_summary_memory() {
  SummaryMemory memory;
  memory.summary_id = "summary-006-001";
  memory.session_id = "session-006-001";
  memory.summary_text = "本轮完成了检索、验证和回复汇总。";
  memory.created_at = 1710000310000;
  return memory;
}

// Factory for a fully populated SummaryMemory contract object.
SummaryMemory make_full_summary_memory() {
  auto memory = make_valid_summary_memory();
  memory.source_turn_ids = std::vector<std::string>{"turn-006-001", "turn-006-002"};
  memory.decisions_made = std::vector<std::string>{"继续保留检索路径"};
  memory.confirmed_facts = std::vector<std::string>{"工具搜索返回了两个稳定结果"};
  memory.tool_outcomes = std::vector<std::string>{"knowledge_search:success"};
  memory.tags = std::vector<std::string>{"memory", "summary"};
  return memory;
}

// -------------------------------------------------------------------------
// Turn validation coverage
// -------------------------------------------------------------------------

void test_valid_turn_passes_required_fields() {
  const auto result = validate_turn_required_fields(make_valid_turn());
  assert_true(result.ok, "minimal valid Turn should pass required-field validation");
}

void test_full_turn_passes_field_rules() {
  const auto result = validate_turn_field_rules(make_full_turn());
  assert_true(result.ok, "full valid Turn should pass field-rule validation");
}

void test_missing_turn_id_is_rejected() {
  auto turn = make_valid_turn();
  turn.turn_id = std::nullopt;
  const auto result = validate_turn_required_fields(turn);

  assert_true(!result.ok, "missing turn_id must be rejected");
  assert_equal("turn_id is required and must be non-empty",
               std::string(result.reason),
               "missing turn_id should report the canonical reason");
}

void test_duplicate_tool_call_refs_are_rejected() {
  auto turn = make_valid_turn();
  turn.tool_call_refs = std::vector<std::string>{"tool-call-1", "tool-call-1"};
  const auto result = validate_turn_field_rules(turn);

  assert_true(!result.ok, "duplicate tool_call_refs must be rejected");
  assert_equal("tool_call_refs must not contain duplicate items",
               std::string(result.reason),
               "duplicate tool_call_refs should report the canonical reason");
}

void test_turn_rejects_checkpoint_field_boundary() {
  const auto result = evaluate_turn_field_boundary("pending_action");

  assert_true(!result.allowed, "pending_action must be rejected for Turn");
  assert_equal(static_cast<int>(TurnBoundaryDecision::RejectCheckpointField),
               static_cast<int>(result.decision),
               "pending_action should map to RejectCheckpointField");
}

void test_turn_rejects_session_context_field_boundary() {
  const auto result = evaluate_turn_field_boundary("recent_history");

  assert_true(!result.allowed, "recent_history must be rejected for Turn");
  assert_equal(static_cast<int>(TurnBoundaryDecision::RejectSessionContextField),
               static_cast<int>(result.decision),
               "recent_history should map to RejectSessionContextField");
}

// -------------------------------------------------------------------------
// Session validation coverage
// -------------------------------------------------------------------------

void test_valid_session_passes_required_fields() {
  const auto result = validate_session_required_fields(make_valid_session());
  assert_true(result.ok, "minimal valid Session should pass required-field validation");
}

void test_full_session_passes_field_rules() {
  const auto result = validate_session_field_rules(make_full_session());
  assert_true(result.ok, "full valid Session should pass field-rule validation");
}

void test_missing_turn_ids_is_rejected() {
  auto session = make_valid_session();
  session.turn_ids = std::nullopt;
  const auto result = validate_session_required_fields(session);

  assert_true(!result.ok, "missing turn_ids must be rejected");
  assert_equal(
      "turn_ids is required (use an empty vector for a new session)",
      std::string(result.reason),
      "missing turn_ids should report the canonical reason");
}

void test_session_last_active_before_created_at_is_rejected() {
  auto session = make_full_session();
  session.last_active_at = 1710000200000;
  const auto result = validate_session_field_rules(session);

  assert_true(!result.ok, "last_active_at earlier than created_at must be rejected");
  assert_equal("last_active_at must not be earlier than created_at",
               std::string(result.reason),
               "time reversal should report the canonical reason");
}

void test_session_rejects_session_context_field_boundary() {
  const auto result = evaluate_session_field_boundary("skill_profile");

  assert_true(!result.allowed, "skill_profile must be rejected for Session");
  assert_equal(static_cast<int>(SessionBoundaryDecision::RejectSessionContextField),
               static_cast<int>(result.decision),
               "skill_profile should map to RejectSessionContextField");
}

void test_session_rejects_checkpoint_field_boundary() {
  const auto result = evaluate_session_field_boundary("checkpoint_id");

  assert_true(!result.allowed, "checkpoint_id must be rejected for Session");
  assert_equal(static_cast<int>(SessionBoundaryDecision::RejectCheckpointField),
               static_cast<int>(result.decision),
               "checkpoint_id should map to RejectCheckpointField");
}

// -------------------------------------------------------------------------
// SummaryMemory validation coverage
// -------------------------------------------------------------------------

void test_valid_summary_memory_passes_required_fields() {
  const auto result =
      validate_summary_memory_required_fields(make_valid_summary_memory());
  assert_true(result.ok,
              "minimal valid SummaryMemory should pass required-field validation");
}

void test_full_summary_memory_passes_field_rules() {
  const auto result = validate_summary_memory_field_rules(make_full_summary_memory());
  assert_true(result.ok,
              "full valid SummaryMemory should pass field-rule validation");
}

void test_missing_summary_text_is_rejected() {
  auto memory = make_valid_summary_memory();
  memory.summary_text = std::nullopt;
  const auto result = validate_summary_memory_required_fields(memory);

  assert_true(!result.ok, "missing summary_text must be rejected");
  assert_equal("summary_text is required and must be non-empty",
               std::string(result.reason),
               "missing summary_text should report the canonical reason");
}

void test_duplicate_confirmed_facts_are_rejected() {
  auto memory = make_valid_summary_memory();
  memory.confirmed_facts = std::vector<std::string>{"fact-1", "fact-1"};
  const auto result = validate_summary_memory_field_rules(memory);

  assert_true(!result.ok, "duplicate confirmed_facts must be rejected");
  assert_equal("confirmed_facts must not contain duplicate items",
               std::string(result.reason),
               "duplicate confirmed_facts should report the canonical reason");
}

void test_summary_memory_rejects_checkpoint_field_boundary() {
  const auto result = evaluate_summary_memory_field_boundary("working_memory_snapshot");

  assert_true(!result.allowed,
              "working_memory_snapshot must be rejected for SummaryMemory");
  assert_equal(static_cast<int>(SummaryMemoryBoundaryDecision::RejectCheckpointField),
               static_cast<int>(result.decision),
               "working_memory_snapshot should map to RejectCheckpointField");
}

void test_summary_memory_rejects_session_context_field_boundary() {
  const auto result = evaluate_summary_memory_field_boundary("policy_digest");

  assert_true(!result.allowed, "policy_digest must be rejected for SummaryMemory");
  assert_equal(
      static_cast<int>(SummaryMemoryBoundaryDecision::RejectSessionContextField),
      static_cast<int>(result.decision),
      "policy_digest should map to RejectSessionContextField");
}

void test_summary_memory_rejects_execution_record_field_boundary() {
  const auto result = evaluate_summary_memory_field_boundary("payload");

  assert_true(!result.allowed, "payload must be rejected for SummaryMemory");
  assert_equal(
      static_cast<int>(SummaryMemoryBoundaryDecision::RejectExecutionRecordField),
      static_cast<int>(result.decision),
      "payload should map to RejectExecutionRecordField");
}

}  // namespace

int main() {
  try {
    test_valid_turn_passes_required_fields();
    test_full_turn_passes_field_rules();
    test_missing_turn_id_is_rejected();
    test_duplicate_tool_call_refs_are_rejected();
    test_turn_rejects_checkpoint_field_boundary();
    test_turn_rejects_session_context_field_boundary();

    test_valid_session_passes_required_fields();
    test_full_session_passes_field_rules();
    test_missing_turn_ids_is_rejected();
    test_session_last_active_before_created_at_is_rejected();
    test_session_rejects_session_context_field_boundary();
    test_session_rejects_checkpoint_field_boundary();

    test_valid_summary_memory_passes_required_fields();
    test_full_summary_memory_passes_field_rules();
    test_missing_summary_text_is_rejected();
    test_duplicate_confirmed_facts_are_rejected();
    test_summary_memory_rejects_checkpoint_field_boundary();
    test_summary_memory_rejects_session_context_field_boundary();
    test_summary_memory_rejects_execution_record_field_boundary();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}