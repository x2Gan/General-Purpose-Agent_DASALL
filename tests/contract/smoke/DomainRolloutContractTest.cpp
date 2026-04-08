#include <exception>
#include <iostream>
#include <string>

#include "boundary/DomainRolloutGuards.h"
#include "support/TestAssertions.h"

namespace {

using dasall::contracts::DomainRolloutDecision;
using dasall::contracts::DomainRolloutSnapshot;
using dasall::contracts::DomainRolloutWave;
using dasall::contracts::DomainSubdomain;
using dasall::contracts::count_completed_domain_rollouts;
using dasall::contracts::domain_rollout_name;
using dasall::contracts::domain_rollout_wave;
using dasall::contracts::evaluate_domain_rollout_start;
using dasall::contracts::kDomainRolloutNames;
using dasall::contracts::kDomainRolloutOrder;

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

void test_rollout_catalog_alignment() {
  assert_equal(static_cast<int>(kDomainRolloutOrder.size()),
               static_cast<int>(kDomainRolloutNames.size()),
               "domain rollout names must align with rollout order catalog");
  assert_equal(std::string("tool"),
               std::string(domain_rollout_name(kDomainRolloutOrder.front())),
               "first rollout domain must be tool");
  assert_equal(std::string("llm"),
               std::string(domain_rollout_name(kDomainRolloutOrder.back())),
               "last rollout domain must be llm");
}

void test_tool_can_start_first_wave() {
  const auto result = evaluate_domain_rollout_start(DomainRolloutSnapshot{},
                                                    DomainSubdomain::Tool);

  assert_true(result.allowed,
              "tool rollout must be allowed on an empty snapshot");
  assert_equal(static_cast<int>(DomainRolloutDecision::AllowStart),
               static_cast<int>(result.decision),
               "tool should enter rollout with allow decision");
  assert_equal(static_cast<int>(DomainRolloutWave::Wave1Tool),
               static_cast<int>(result.required_wave),
               "tool must map to wave 1");
}

void test_prompt_requires_tool_completion() {
  const auto result = evaluate_domain_rollout_start(DomainRolloutSnapshot{},
                                                    DomainSubdomain::Prompt);

  assert_true(!result.allowed,
              "prompt rollout must be blocked before tool completes");
  assert_equal(static_cast<int>(DomainRolloutDecision::RejectMissingPrerequisite),
               static_cast<int>(result.decision),
               "prompt should report missing prerequisite before wave 2");
  assert_equal(std::string("tool"),
               std::string(result.missing_prerequisite),
               "prompt should name tool as the missing prerequisite");
}

void test_prompt_and_memory_can_run_in_parallel() {
  DomainRolloutSnapshot snapshot;
  snapshot.tool_completed = true;
  snapshot.prompt_in_progress = true;

  const auto result = evaluate_domain_rollout_start(snapshot,
                                                    DomainSubdomain::Memory);

  assert_true(result.allowed,
              "memory rollout must be allowed to parallelize with prompt in wave 2");
  assert_equal(static_cast<int>(DomainRolloutWave::Wave2PromptMemory),
               static_cast<int>(result.required_wave),
               "memory must map to wave 2");
}

void test_task_requires_wave_two_completion() {
  DomainRolloutSnapshot snapshot;
  snapshot.tool_completed = true;
  snapshot.prompt_completed = true;
  snapshot.memory_in_progress = true;

  const auto result = evaluate_domain_rollout_start(snapshot,
                                                    DomainSubdomain::Task);

  assert_true(!result.allowed,
              "task rollout must be blocked until prompt and memory are both complete");
  assert_equal(static_cast<int>(DomainRolloutDecision::RejectMissingPrerequisite),
               static_cast<int>(result.decision),
               "task should fail on missing prerequisite before wave 3");
  assert_equal(std::string("memory"),
               std::string(result.missing_prerequisite),
               "task should report memory as the last missing prerequisite");
}

void test_task_and_event_can_run_in_parallel() {
  DomainRolloutSnapshot snapshot;
  snapshot.tool_completed = true;
  snapshot.prompt_completed = true;
  snapshot.memory_completed = true;
  snapshot.task_in_progress = true;

  const auto result = evaluate_domain_rollout_start(snapshot,
                                                    DomainSubdomain::Event);

  assert_true(result.allowed,
              "event rollout must be allowed to parallelize with task in wave 3");
  assert_equal(static_cast<int>(DomainRolloutWave::Wave3TaskEvent),
               static_cast<int>(result.required_wave),
               "event must map to wave 3");
}

void test_llm_requires_all_previous_waves_completed() {
  DomainRolloutSnapshot snapshot;
  snapshot.tool_completed = true;
  snapshot.prompt_completed = true;
  snapshot.memory_completed = true;
  snapshot.task_completed = true;

  const auto result = evaluate_domain_rollout_start(snapshot,
                                                    DomainSubdomain::Llm);

  assert_true(!result.allowed,
              "llm rollout must wait until event completes");
  assert_equal(static_cast<int>(DomainRolloutDecision::RejectMissingPrerequisite),
               static_cast<int>(result.decision),
               "llm should report a missing prerequisite before wave 4");
  assert_equal(std::string("event"),
               std::string(result.missing_prerequisite),
               "llm should name event as the final missing prerequisite");
}

void test_wave_boundary_violation_is_rejected() {
  DomainRolloutSnapshot snapshot;
  snapshot.tool_completed = true;
  snapshot.task_in_progress = true;

  const auto result = evaluate_domain_rollout_start(snapshot,
                    DomainSubdomain::Prompt);

  assert_true(!result.allowed,
      "prompt rollout must reject cross-wave parallel start while wave 3 is active");
  assert_equal(
  static_cast<int>(DomainRolloutDecision::RejectParallelBoundaryViolation),
      static_cast<int>(result.decision),
  "prompt should report wave-boundary violation when wave 3 is active");
}

void test_duplicate_domain_start_is_rejected() {
  DomainRolloutSnapshot snapshot;
  snapshot.tool_completed = true;

  const auto result = evaluate_domain_rollout_start(snapshot,
                                                    DomainSubdomain::Tool);

  assert_true(!result.allowed,
              "completed domains must not re-enter rollout");
  assert_equal(static_cast<int>(DomainRolloutDecision::RejectAlreadyCompleted),
               static_cast<int>(result.decision),
               "duplicate rollout should return completed rejection");
}

void test_completed_count_tracks_finished_domains() {
  DomainRolloutSnapshot snapshot;
  snapshot.tool_completed = true;
  snapshot.prompt_completed = true;
  snapshot.memory_completed = true;
  snapshot.event_completed = true;

  assert_equal(4,
               static_cast<int>(count_completed_domain_rollouts(snapshot)),
               "completed count must match the number of finished rollout domains");
}

}  // namespace

int main() {
  int passed = 0;
  int failed = 0;

  auto run_test = [&](const char* name, void (*fn)()) {
    try {
      fn();
      ++passed;
      std::cout << "  PASS: " << name << "\n";
    } catch (const std::exception& ex) {
      ++failed;
      std::cerr << "  FAIL: " << name << " - " << ex.what() << "\n";
    }
  };

  std::cout << "DomainRolloutContractTest - WP05-T001-B\n";

  run_test("test_rollout_catalog_alignment", test_rollout_catalog_alignment);
  run_test("test_tool_can_start_first_wave", test_tool_can_start_first_wave);
  run_test("test_prompt_requires_tool_completion",
           test_prompt_requires_tool_completion);
  run_test("test_prompt_and_memory_can_run_in_parallel",
           test_prompt_and_memory_can_run_in_parallel);
  run_test("test_task_requires_wave_two_completion",
           test_task_requires_wave_two_completion);
  run_test("test_task_and_event_can_run_in_parallel",
           test_task_and_event_can_run_in_parallel);
  run_test("test_llm_requires_all_previous_waves_completed",
           test_llm_requires_all_previous_waves_completed);
  run_test("test_wave_boundary_violation_is_rejected",
           test_wave_boundary_violation_is_rejected);
  run_test("test_duplicate_domain_start_is_rejected",
           test_duplicate_domain_start_is_rejected);
  run_test("test_completed_count_tracks_finished_domains",
           test_completed_count_tracks_finished_domains);

  std::cout << "\nResults: " << passed << " passed, " << failed
            << " failed, " << (passed + failed) << " total\n";

  return (failed > 0) ? 1 : 0;
}