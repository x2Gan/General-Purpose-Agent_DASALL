#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "dasall/tests/support/TestAssertions.h"
#include "prompt/PromptRelease.h"
#include "prompt/PromptReleaseGuards.h"
#include "prompt/PromptSpec.h"

namespace {

using dasall::contracts::CompositionStage;
using dasall::contracts::PromptEvalStatus;
using dasall::contracts::PromptRelease;
using dasall::contracts::PromptReleaseBoundaryDecision;
using dasall::contracts::PromptSpec;
using dasall::contracts::PromptSpecBoundaryDecision;
using dasall::contracts::evaluate_prompt_release_field_boundary;
using dasall::contracts::evaluate_prompt_spec_field_boundary;
using dasall::contracts::validate_prompt_release_field_rules;
using dasall::contracts::validate_prompt_release_required_fields;
using dasall::contracts::validate_prompt_spec_field_rules;
using dasall::contracts::validate_prompt_spec_required_fields;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

PromptSpec make_valid_prompt_spec() {
  PromptSpec spec;
  spec.prompt_id = "prompt.plan.default";
  spec.stage = CompositionStage::Planning;
  spec.template_slots = std::vector<std::string>{"goal_summary", "constraints"};
  spec.task_types = std::vector<std::string>{"plan_step"};
  spec.language = "zh-CN";
  spec.model_family = "gpt-5";
  spec.output_schema_ref = "schema://prompt/plan/default/v1";
  spec.tool_hints = std::vector<std::string>{"tool.search", "tool.summarize"};
  spec.tags = std::vector<std::string>{"planning", "default"};
  return spec;
}

PromptRelease make_valid_prompt_release() {
  PromptRelease release;
  release.prompt_id = "prompt.plan.default";
  release.version = "2026-03-20.1";
  release.stage = CompositionStage::Planning;
  release.eval_status = PromptEvalStatus::Canary;
  release.release_scope = "desktop_full";
  release.system_instructions = "You are the planning stage prompt.";
  release.task_template = "Summarize the goal and generate the next plan step.";
  release.output_schema_ref = "schema://prompt/plan/default/v1";
  release.few_shot_refs = std::vector<std::string>{"fewshot://plan/default/01"};
  release.policy_notes = std::vector<std::string>{"Do not enable hidden tools."};
  release.rollback_from = "2026-03-18.4";
  release.trusted_source = "repo://skills/prompts/plan";
  release.tags = std::vector<std::string>{"planning", "canary"};
  return release;
}

void test_valid_prompt_spec_passes_required_fields() {
  const auto result = validate_prompt_spec_required_fields(make_valid_prompt_spec());
  assert_true(result.ok, "valid PromptSpec should pass required-field validation");
}

void test_valid_prompt_spec_passes_field_rules() {
  const auto result = validate_prompt_spec_field_rules(make_valid_prompt_spec());
  assert_true(result.ok, "valid PromptSpec should pass field-rule validation");
}

void test_prompt_spec_missing_prompt_id_is_rejected() {
  auto spec = make_valid_prompt_spec();
  spec.prompt_id = std::nullopt;
  const auto result = validate_prompt_spec_required_fields(spec);

  assert_true(!result.ok, "missing prompt_id must be rejected");
  assert_equal("prompt_id is required and must be non-empty",
               std::string(result.reason),
               "missing prompt_id should report the canonical reason");
}

void test_prompt_spec_duplicate_template_slots_are_rejected() {
  auto spec = make_valid_prompt_spec();
  spec.template_slots = std::vector<std::string>{"goal_summary", "goal_summary"};
  const auto result = validate_prompt_spec_field_rules(spec);

  assert_true(!result.ok, "duplicate template_slots must be rejected");
  assert_equal("template_slots must not contain duplicate items",
               std::string(result.reason),
               "duplicate template_slots should report the canonical reason");
}

void test_prompt_spec_rejects_release_lifecycle_field() {
  const auto result = evaluate_prompt_spec_field_boundary("version");

  assert_true(!result.allowed, "version must be rejected for PromptSpec");
  assert_equal(static_cast<int>(PromptSpecBoundaryDecision::RejectReleaseLifecycleField),
               static_cast<int>(result.decision),
               "version should map to RejectReleaseLifecycleField");
}

void test_prompt_spec_rejects_compose_result_field() {
  const auto result = evaluate_prompt_spec_field_boundary("messages");

  assert_true(!result.allowed, "messages must be rejected for PromptSpec");
  assert_equal(static_cast<int>(PromptSpecBoundaryDecision::RejectComposeResultField),
               static_cast<int>(result.decision),
               "messages should map to RejectComposeResultField");
}

void test_valid_prompt_release_passes_required_fields() {
  const auto result = validate_prompt_release_required_fields(make_valid_prompt_release());
  assert_true(result.ok, "valid PromptRelease should pass required-field validation");
}

void test_valid_prompt_release_passes_field_rules() {
  const auto result = validate_prompt_release_field_rules(make_valid_prompt_release());
  assert_true(result.ok, "valid PromptRelease should pass field-rule validation");
}

void test_prompt_release_missing_version_is_rejected() {
  auto release = make_valid_prompt_release();
  release.version = std::nullopt;
  const auto result = validate_prompt_release_required_fields(release);

  assert_true(!result.ok, "missing version must be rejected");
  assert_equal("version is required and must be non-empty",
               std::string(result.reason),
               "missing version should report the canonical reason");
}

void test_prompt_release_out_of_range_eval_status_is_rejected() {
  auto release = make_valid_prompt_release();
  release.eval_status = static_cast<PromptEvalStatus>(99);
  const auto result = validate_prompt_release_field_rules(release);

  assert_true(!result.ok, "out-of-range eval_status must be rejected");
  assert_equal("eval_status value is outside the known PromptEvalStatus enum range",
               std::string(result.reason),
               "out-of-range eval_status should report the canonical reason");
}

void test_prompt_release_rollback_from_self_is_rejected() {
  auto release = make_valid_prompt_release();
  release.rollback_from = *release.version;
  const auto result = validate_prompt_release_field_rules(release);

  assert_true(!result.ok, "rollback_from equal to version must be rejected");
  assert_equal("rollback_from must not equal version",
               std::string(result.reason),
               "self rollback reference should report the canonical reason");
}

void test_prompt_release_rejects_context_ownership_field() {
  const auto result = evaluate_prompt_release_field_boundary("context_packet_id");

  assert_true(!result.allowed, "context_packet_id must be rejected for PromptRelease");
  assert_equal(static_cast<int>(PromptReleaseBoundaryDecision::RejectContextOwnershipField),
               static_cast<int>(result.decision),
               "context_packet_id should map to RejectContextOwnershipField");
}

void test_prompt_release_rejects_writeback_field() {
  const auto result = evaluate_prompt_release_field_boundary("memory_write_back");

  assert_true(!result.allowed, "memory_write_back must be rejected for PromptRelease");
  assert_equal(static_cast<int>(PromptReleaseBoundaryDecision::RejectWritebackField),
               static_cast<int>(result.decision),
               "memory_write_back should map to RejectWritebackField");
}

void test_regular_fields_remain_allowed() {
  const auto spec_result = evaluate_prompt_spec_field_boundary("template_slots");
  assert_true(spec_result.allowed, "template_slots should remain allowed in PromptSpec");

  const auto release_result = evaluate_prompt_release_field_boundary("task_template");
  assert_true(release_result.allowed, "task_template should remain allowed in PromptRelease");
}

}  // namespace

int main() {
  try {
    test_valid_prompt_spec_passes_required_fields();
    test_valid_prompt_spec_passes_field_rules();
    test_prompt_spec_missing_prompt_id_is_rejected();
    test_prompt_spec_duplicate_template_slots_are_rejected();
    test_prompt_spec_rejects_release_lifecycle_field();
    test_prompt_spec_rejects_compose_result_field();
    test_valid_prompt_release_passes_required_fields();
    test_valid_prompt_release_passes_field_rules();
    test_prompt_release_missing_version_is_rejected();
    test_prompt_release_out_of_range_eval_status_is_rejected();
    test_prompt_release_rollback_from_self_is_rejected();
    test_prompt_release_rejects_context_ownership_field();
    test_prompt_release_rejects_writeback_field();
    test_regular_fields_remain_allowed();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}