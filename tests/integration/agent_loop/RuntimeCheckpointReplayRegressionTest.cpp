#include <algorithm>
#include <cctype>
#include <exception>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include "AgentFacade.h"
#include "IAgent.h"
#include "RuntimeUnaryFixture.h"
#include "agent/AgentResult.h"
#include "checkpoint/Checkpoint.h"
#include "checkpoint/CheckpointBuildTypes.h"
#include "support/TestAssertions.h"

namespace {

using dasall::contracts::Checkpoint;
using dasall::contracts::CheckpointState;
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

void test_valid_waiting_tool_fixture_replays_through_runtime_resume_path() {
  using dasall::contracts::AgentResultStatus;
  using dasall::runtime::AgentFacade;
  using dasall::runtime::IAgent;
  using dasall::runtime::PendingInteractionKind;
  using dasall::runtime::RuntimeState;
  using dasall::tests::runtime_fixture::make_init_request;
  using dasall::tests::runtime_fixture::make_resume_request;
  using dasall::tests::runtime_fixture::make_seeded_resume_dependency_set;
  using dasall::tests::runtime_fixture::make_waiting_session_snapshot;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const auto checkpoint = load_checkpoint_fixture("replay_waiting_tool_v1.fixture");
  const auto session_snapshot = make_waiting_session_snapshot(
      "session-028-replay",
      checkpoint.request_id.value_or(std::string("req-024")),
      checkpoint.checkpoint_id.value_or(std::string()),
      RuntimeState::WaitingExternal,
      PendingInteractionKind::WaitExternal,
      checkpoint.pending_action.value_or(std::string("await tool callback")),
      "tool_callback",
      "application/json");

  std::unique_ptr<IAgent> agent = std::make_unique<AgentFacade>();
  const auto init_result = agent->init(make_init_request(
      "rt-028-replay",
      "desktop_full",
      "runtime-replay-fixture",
      make_seeded_resume_dependency_set(session_snapshot, checkpoint)));
  assert_true(init_result.is_ready(), "replay regression requires a ready facade");

  const auto result = agent->resume(make_resume_request(
      session_snapshot.session_id,
      checkpoint.checkpoint_id.value_or(std::string()),
      "resume-028-replay",
      "external tool result received",
      "resume-token-028-replay",
      "trace-028-replay"));

  assert_true(result.status.has_value() && *result.status == AgentResultStatus::Completed,
              "valid waiting-tool fixture should replay to a completed result");
  assert_true(result.task_completed == true,
              "valid waiting-tool fixture should complete the replayed turn");
  assert_true(result.checkpoint_ref.has_value() &&
                  result.checkpoint_ref != checkpoint.checkpoint_id,
              "valid replay path should emit a new completion checkpoint");
  assert_equal("runtime orchestrator skeleton completed",
               result.response_text.value_or(std::string()),
               "valid replay path should return the direct-success response");
}

void test_schema_mismatch_fixture_is_rejected_through_runtime_resume_path() {
  using dasall::contracts::AgentResultStatus;
  using dasall::runtime::AgentFacade;
  using dasall::runtime::IAgent;
  using dasall::runtime::PendingInteractionKind;
  using dasall::runtime::RuntimeState;
  using dasall::tests::runtime_fixture::make_init_request;
  using dasall::tests::runtime_fixture::make_resume_request;
  using dasall::tests::runtime_fixture::make_seeded_resume_dependency_set;
  using dasall::tests::runtime_fixture::make_waiting_session_snapshot;
  using dasall::tests::support::assert_true;

  const auto checkpoint = load_checkpoint_fixture("replay_waiting_tool_schema_v2.fixture");
  const auto session_snapshot = make_waiting_session_snapshot(
      "session-028-schema",
      checkpoint.request_id.value_or(std::string("req-024-schema")),
      checkpoint.checkpoint_id.value_or(std::string()),
      RuntimeState::WaitingExternal,
      PendingInteractionKind::WaitExternal,
      checkpoint.pending_action.value_or(std::string("await tool callback")),
      "tool_callback",
      "application/json");

  std::unique_ptr<IAgent> agent = std::make_unique<AgentFacade>();
  const auto init_result = agent->init(make_init_request(
      "rt-028-schema",
      "desktop_full",
      "runtime-replay-fixture",
      make_seeded_resume_dependency_set(session_snapshot, checkpoint)));
  assert_true(init_result.is_ready(), "schema reject regression requires a ready facade");

  const auto result = agent->resume(make_resume_request(
      session_snapshot.session_id,
      checkpoint.checkpoint_id.value_or(std::string()),
      "resume-028-schema",
      "external tool result received",
      "resume-token-028-schema",
      "trace-028-schema"));

  assert_true(result.status.has_value() && *result.status == AgentResultStatus::Failed,
              "schema-v2 fixture must be rejected through facade resume path");
  assert_true(result.task_completed == false,
              "schema-v2 fixture rejection must not mark task_completed=true");
  assert_true(result.response_text.has_value() &&
                  result.response_text->find("load resume checkpoint") != std::string::npos,
              "schema-v2 rejection should fail while loading the seeded checkpoint");
  assert_true(result.error_info.has_value() &&
            result.error_info->details.message.find("rt.schema_version is not compatible") !=
              std::string::npos,
              "schema-v2 rejection should preserve the unsupported-version detail");
}

}  // namespace

int main() {
  try {
    test_valid_waiting_tool_fixture_replays_through_runtime_resume_path();
    test_schema_mismatch_fixture_is_rejected_through_runtime_resume_path();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}