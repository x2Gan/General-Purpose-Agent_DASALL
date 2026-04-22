#include <algorithm>
#include <cctype>
#include <exception>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "checkpoint/CheckpointBuildTypes.h"
#include "checkpoint/CheckpointManager.h"
#include "recovery/ResumePlan.h"
#include "support/TestAssertions.h"

namespace {

using dasall::contracts::Checkpoint;
using dasall::contracts::CheckpointState;
using dasall::runtime::CheckpointConsistencyIssue;
using dasall::runtime::CheckpointManager;
using dasall::runtime::ResumePlanViolation;
using dasall::runtime::RuntimeErrorCode;
using dasall::runtime::RuntimeState;
using dasall::runtime::make_checkpoint_tag;

[[nodiscard]] std::string trim(std::string value) {
  auto is_space = [](unsigned char ch) { return std::isspace(ch) != 0; };

  while (!value.empty() && is_space(static_cast<unsigned char>(value.front()))) {
    value.erase(value.begin());
  }
  while (!value.empty() && is_space(static_cast<unsigned char>(value.back()))) {
    value.pop_back();
  }
  return value;
}

[[nodiscard]] CheckpointState parse_checkpoint_state(const std::string& raw) {
  if (raw == "Running") {
    return CheckpointState::Running;
  }
  if (raw == "Paused") {
    return CheckpointState::Paused;
  }
  if (raw == "WaitingConfirm") {
    return CheckpointState::WaitingConfirm;
  }
  if (raw == "WaitingTool") {
    return CheckpointState::WaitingTool;
  }
  if (raw == "Failed") {
    return CheckpointState::Failed;
  }
  if (raw == "Succeeded") {
    return CheckpointState::Succeeded;
  }

  throw std::runtime_error("unknown checkpoint state in fixture: " + raw);
}

[[nodiscard]] std::string fixture_path(const std::string& file_name) {
  return std::string(DASALL_RUNTIME_FIXTURE_CHECKPOINT_DIR) + "/" + file_name;
}

[[nodiscard]] Checkpoint load_checkpoint_fixture(const std::string& file_name) {
  std::ifstream input(fixture_path(file_name));
  if (!input.is_open()) {
    throw std::runtime_error("failed to open checkpoint fixture: " + file_name);
  }

  Checkpoint checkpoint;
  std::vector<std::string> tags;
  std::string line;
  while (std::getline(input, line)) {
    line = trim(line);
    if (line.empty() || line.rfind("#", 0) == 0) {
      continue;
    }

    const auto delimiter = line.find('=');
    if (delimiter == std::string::npos) {
      throw std::runtime_error("invalid fixture line: " + line);
    }

    const auto key = trim(line.substr(0, delimiter));
    const auto value = trim(line.substr(delimiter + 1));

    if (key == "checkpoint_id") {
      checkpoint.checkpoint_id = value;
    } else if (key == "state") {
      checkpoint.state = parse_checkpoint_state(value);
    } else if (key == "step_id") {
      checkpoint.step_id = value;
    } else if (key == "working_memory_snapshot") {
      checkpoint.working_memory_snapshot = value;
    } else if (key == "pending_action") {
      checkpoint.pending_action = value;
    } else if (key == "request_id") {
      checkpoint.request_id = value;
    } else if (key == "goal_id") {
      checkpoint.goal_id = value;
    } else if (key == "belief_state_ref") {
      checkpoint.belief_state_ref = value;
    } else if (key == "created_at") {
      checkpoint.created_at = std::stoll(value);
    } else if (key.rfind("tag.", 0) == 0) {
      tags.push_back(make_checkpoint_tag(key.substr(4), value));
    } else {
      throw std::runtime_error("unknown fixture key: " + key);
    }
  }

  if (!tags.empty()) {
    checkpoint.tags = tags;
  }

  return checkpoint;
}

void test_valid_waiting_tool_fixture_replays_to_a_stable_resume_plan() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  CheckpointManager manager;
  const auto checkpoint = load_checkpoint_fixture("replay_waiting_tool_v1.fixture");

  const auto validation_report = manager.validate(checkpoint);
  assert_true(validation_report.consistent,
              "valid waiting-tool fixture should pass checkpoint validation");

  manager.seed_for_test(checkpoint);
  const auto first_load = manager.load(checkpoint.checkpoint_id.value_or(std::string()));
  const auto second_load = manager.load(checkpoint.checkpoint_id.value_or(std::string()));

  assert_true(first_load.loaded(), "first replay load should succeed for valid fixture");
  assert_true(second_load.loaded(), "second replay load should also succeed for valid fixture");

  const auto first_plan = manager.make_resume_plan(*first_load.checkpoint);
  const auto second_plan = manager.make_resume_plan(*second_load.checkpoint);

  assert_true(first_plan.resumable,
              "valid waiting-tool fixture should synthesize a resume plan");
  assert_true(second_plan.resumable,
              "repeated replay load should remain resumable");
  assert_true(first_plan.plan->target_state == RuntimeState::WaitingExternal,
              "waiting-tool checkpoint should map to WaitingExternal runtime state");
  assert_true(second_plan.plan->target_state == RuntimeState::WaitingExternal,
              "repeated load should keep the same target runtime state");
  assert_equal(first_plan.plan->checkpoint_ref,
               second_plan.plan->checkpoint_ref,
               "repeated replay load should preserve checkpoint_ref stability");
  assert_equal(first_plan.plan->pending_action.value_or(std::string()),
               second_plan.plan->pending_action.value_or(std::string()),
               "repeated replay load should preserve pending_action stability");
  assert_true(!first_plan.plan->requires_operator_intervention,
              "waiting-tool fixture should not request operator intervention");
}

void test_schema_version_mismatch_fixture_is_explicitly_rejected() {
  using dasall::tests::support::assert_true;

  CheckpointManager manager;
  const auto checkpoint = load_checkpoint_fixture("replay_waiting_tool_schema_v2.fixture");

  manager.seed_for_test(checkpoint);
  const auto load_result = manager.load(checkpoint.checkpoint_id.value_or(std::string()));
  assert_true(!load_result.loaded(),
              "schema-v2 fixture must be rejected during load");
  assert_true(load_result.report.issue == CheckpointConsistencyIssue::UnsupportedSchemaVersion,
              "schema-v2 fixture should classify as UnsupportedSchemaVersion");
  assert_true(load_result.error_code == RuntimeErrorCode::RT_E_412_RESUME_REJECTED,
              "schema-v2 fixture should map to RT_E_412_RESUME_REJECTED");

  const auto resume_plan = manager.make_resume_plan(checkpoint);
  assert_true(!resume_plan.resumable,
              "schema-v2 fixture must not synthesize a resume plan");
  assert_true(resume_plan.violation == ResumePlanViolation::CheckpointInvalid,
              "schema-v2 fixture should reject as a checkpoint-invalid resume request");
}

void test_waiting_tool_fixture_without_pending_action_is_rejected() {
  using dasall::tests::support::assert_true;

  CheckpointManager manager;
  const auto checkpoint =
      load_checkpoint_fixture("replay_waiting_tool_missing_pending_action.fixture");

  const auto validation_report = manager.validate(checkpoint);
  assert_true(!validation_report.consistent,
              "waiting-tool fixture missing pending_action must fail validation");
  assert_true(validation_report.issue == CheckpointConsistencyIssue::MissingPendingAction,
              "missing pending_action should classify as MissingPendingAction");

  const auto resume_plan = manager.make_resume_plan(checkpoint);
  assert_true(!resume_plan.resumable,
              "waiting-tool fixture missing pending_action must not resume");
  assert_true(resume_plan.violation == ResumePlanViolation::CheckpointInvalid,
              "missing pending_action should reject through checkpoint-invalid path");
}

void test_terminal_fixture_remains_structurally_valid_but_non_resumable() {
  using dasall::tests::support::assert_true;

  CheckpointManager manager;
  const auto checkpoint = load_checkpoint_fixture("replay_terminal_succeeded_v1.fixture");

  const auto validation_report = manager.validate(checkpoint);
  assert_true(validation_report.consistent,
              "terminal fixture should stay structurally valid for audit retention");

  const auto resume_plan = manager.make_resume_plan(checkpoint);
  assert_true(!resume_plan.resumable,
              "terminal checkpoint fixture must not synthesize a resume plan");
  assert_true(resume_plan.violation == ResumePlanViolation::UnsupportedCheckpointState,
              "terminal checkpoint should reject as UnsupportedCheckpointState");
  assert_true(resume_plan.error_code == RuntimeErrorCode::RT_E_412_RESUME_REJECTED,
              "terminal checkpoint rejection should surface RT_E_412_RESUME_REJECTED");
}

}  // namespace

int main() {
  try {
    test_valid_waiting_tool_fixture_replays_to_a_stable_resume_plan();
    test_schema_version_mismatch_fixture_is_explicitly_rejected();
    test_waiting_tool_fixture_without_pending_action_is_rejected();
    test_terminal_fixture_remains_structurally_valid_but_non_resumable();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}