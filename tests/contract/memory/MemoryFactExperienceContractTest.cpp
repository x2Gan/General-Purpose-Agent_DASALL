#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "support/TestAssertions.h"
#include "memory/ExperienceMemory.h"
#include "memory/MemoryFact.h"

namespace {

using dasall::contracts::ExperienceMemory;
using dasall::contracts::ExperienceMemoryBoundaryDecision;
using dasall::contracts::MemoryFact;
using dasall::contracts::MemoryFactBoundaryDecision;
using dasall::contracts::evaluate_experience_memory_field_boundary;
using dasall::contracts::evaluate_memory_fact_field_boundary;
using dasall::contracts::validate_experience_memory_field_rules;
using dasall::contracts::validate_experience_memory_required_fields;
using dasall::contracts::validate_memory_fact_field_rules;
using dasall::contracts::validate_memory_fact_required_fields;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

// Factory for a minimally valid MemoryFact contract object.
MemoryFact make_valid_memory_fact() {
  MemoryFact fact;
  fact.fact_id = "fact-007-001";
  fact.session_id = "session-007-001";
  fact.fact_text = "用户偏好在输出中保留执行证据。";
  fact.source_turn_ids = std::vector<std::string>{"turn-007-001"};
  fact.confidence_score = 92;
  fact.created_at = 1710000400000;
  return fact;
}

// Factory for a fully populated MemoryFact contract object.
MemoryFact make_full_memory_fact() {
  auto fact = make_valid_memory_fact();
  fact.fact_type = "preference";
  fact.source_observation_refs = std::vector<std::string>{"obs-007-001", "obs-007-002"};
  fact.valid_until = 1710100400000;
  fact.evidence_digest = R"({"source":"observation","count":2})";
  fact.tags = std::vector<std::string>{"memory", "fact"};
  return fact;
}

// Factory for a minimally valid ExperienceMemory contract object.
ExperienceMemory make_valid_experience_memory() {
  ExperienceMemory experience;
  experience.experience_id = "exp-007-001";
  experience.session_id = "session-007-001";
  experience.lesson_summary = "当信息不完整时先执行澄清提问再调用高成本工具。";
  experience.trigger_condition = "required_fields_missing";
  experience.recommended_action = "issue_clarification_before_tool_call";
  experience.created_at = 1710000410000;
  return experience;
}

// Factory for a fully populated ExperienceMemory contract object.
ExperienceMemory make_full_experience_memory() {
  auto experience = make_valid_experience_memory();
  experience.source_fact_ids = std::vector<std::string>{"fact-007-001"};
  experience.source_turn_ids = std::vector<std::string>{"turn-007-001", "turn-007-002"};
  experience.effectiveness_score = 88;
  experience.applicable_domains = std::vector<std::string>{"memory", "tooling"};
  experience.risk_notes = "仅在澄清成本低于工具调用成本时适用。";
  experience.expires_at = 1710200410000;
  experience.tags = std::vector<std::string>{"memory", "experience"};
  return experience;
}

// -------------------------------------------------------------------------
// MemoryFact validation coverage
// -------------------------------------------------------------------------

void test_valid_memory_fact_passes_required_fields() {
  const auto result = validate_memory_fact_required_fields(make_valid_memory_fact());
  assert_true(result.ok,
              "minimal valid MemoryFact should pass required-field validation");
}

void test_full_memory_fact_passes_field_rules() {
  const auto result = validate_memory_fact_field_rules(make_full_memory_fact());
  assert_true(result.ok, "full valid MemoryFact should pass field-rule validation");
}

void test_memory_fact_missing_fact_text_is_rejected() {
  auto fact = make_valid_memory_fact();
  fact.fact_text = std::nullopt;
  const auto result = validate_memory_fact_required_fields(fact);

  assert_true(!result.ok, "missing fact_text must be rejected");
  assert_equal("fact_text is required and must be non-empty",
               std::string(result.reason),
               "missing fact_text should report the canonical reason");
}

void test_memory_fact_duplicate_source_turn_ids_are_rejected() {
  auto fact = make_valid_memory_fact();
  fact.source_turn_ids = std::vector<std::string>{"turn-007-001", "turn-007-001"};
  const auto result = validate_memory_fact_field_rules(fact);

  assert_true(!result.ok, "duplicate source_turn_ids must be rejected");
  assert_equal("source_turn_ids must not contain duplicate items",
               std::string(result.reason),
               "duplicate source_turn_ids should report the canonical reason");
}

void test_memory_fact_rejects_runtime_control_field_boundary() {
  const auto result = evaluate_memory_fact_field_boundary("retry_count");

  assert_true(!result.allowed, "retry_count must be rejected for MemoryFact");
  assert_equal(static_cast<int>(MemoryFactBoundaryDecision::RejectRuntimeControlField),
               static_cast<int>(result.decision),
               "retry_count should map to RejectRuntimeControlField");
}

void test_memory_fact_rejects_provider_field_boundary() {
  const auto result = evaluate_memory_fact_field_boundary("rendered_prompt");

  assert_true(!result.allowed, "rendered_prompt must be rejected for MemoryFact");
  assert_equal(static_cast<int>(MemoryFactBoundaryDecision::RejectProviderPayloadField),
               static_cast<int>(result.decision),
               "rendered_prompt should map to RejectProviderPayloadField");
}

// -------------------------------------------------------------------------
// ExperienceMemory validation coverage
// -------------------------------------------------------------------------

void test_valid_experience_memory_passes_required_fields() {
  const auto result =
      validate_experience_memory_required_fields(make_valid_experience_memory());
  assert_true(result.ok,
              "minimal valid ExperienceMemory should pass required-field validation");
}

void test_full_experience_memory_passes_field_rules() {
  const auto result = validate_experience_memory_field_rules(make_full_experience_memory());
  assert_true(result.ok,
              "full valid ExperienceMemory should pass field-rule validation");
}

void test_experience_memory_missing_trigger_condition_is_rejected() {
  auto experience = make_valid_experience_memory();
  experience.trigger_condition = std::nullopt;
  const auto result = validate_experience_memory_required_fields(experience);

  assert_true(!result.ok, "missing trigger_condition must be rejected");
  assert_equal("trigger_condition is required and must be non-empty",
               std::string(result.reason),
               "missing trigger_condition should report the canonical reason");
}

void test_experience_memory_duplicate_source_fact_ids_are_rejected() {
  auto experience = make_full_experience_memory();
  experience.source_fact_ids = std::vector<std::string>{"fact-007-001", "fact-007-001"};
  const auto result = validate_experience_memory_field_rules(experience);

  assert_true(!result.ok, "duplicate source_fact_ids must be rejected");
  assert_equal("source_fact_ids must not contain duplicate items",
               std::string(result.reason),
               "duplicate source_fact_ids should report the canonical reason");
}

void test_experience_memory_rejects_prompt_provider_field_boundary() {
  const auto result = evaluate_experience_memory_field_boundary("model_route");

  assert_true(!result.allowed, "model_route must be rejected for ExperienceMemory");
  assert_equal(
      static_cast<int>(ExperienceMemoryBoundaryDecision::RejectPromptProviderField),
      static_cast<int>(result.decision),
      "model_route should map to RejectPromptProviderField");
}

void test_experience_memory_rejects_checkpoint_field_boundary() {
  const auto result = evaluate_experience_memory_field_boundary("checkpoint_id");

  assert_true(!result.allowed, "checkpoint_id must be rejected for ExperienceMemory");
  assert_equal(static_cast<int>(ExperienceMemoryBoundaryDecision::RejectCheckpointField),
               static_cast<int>(result.decision),
               "checkpoint_id should map to RejectCheckpointField");
}

}  // namespace

int main() {
  try {
    test_valid_memory_fact_passes_required_fields();
    test_full_memory_fact_passes_field_rules();
    test_memory_fact_missing_fact_text_is_rejected();
    test_memory_fact_duplicate_source_turn_ids_are_rejected();
    test_memory_fact_rejects_runtime_control_field_boundary();
    test_memory_fact_rejects_provider_field_boundary();

    test_valid_experience_memory_passes_required_fields();
    test_full_experience_memory_passes_field_rules();
    test_experience_memory_missing_trigger_condition_is_rejected();
    test_experience_memory_duplicate_source_fact_ids_are_rejected();
    test_experience_memory_rejects_prompt_provider_field_boundary();
    test_experience_memory_rejects_checkpoint_field_boundary();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}