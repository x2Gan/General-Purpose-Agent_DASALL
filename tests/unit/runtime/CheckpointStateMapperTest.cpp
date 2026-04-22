#include <array>
#include <exception>
#include <iostream>
#include <string>

#include "checkpoint/CheckpointStateMapper.h"
#include "support/TestAssertions.h"

namespace {

[[nodiscard]] constexpr const char* checkpoint_state_name(
    const dasall::contracts::CheckpointState checkpoint_state) {
  using dasall::contracts::CheckpointState;

  switch (checkpoint_state) {
    case CheckpointState::Unspecified:
      return "Unspecified";
    case CheckpointState::Running:
      return "Running";
    case CheckpointState::Paused:
      return "Paused";
    case CheckpointState::WaitingConfirm:
      return "WaitingConfirm";
    case CheckpointState::WaitingTool:
      return "WaitingTool";
    case CheckpointState::Failed:
      return "Failed";
    case CheckpointState::Succeeded:
      return "Succeeded";
  }

  return "Unknown";
}

struct MappingCase {
  dasall::runtime::RuntimeState runtime_state;
  dasall::contracts::CheckpointState checkpoint_state;
};

}  // namespace

int main() {
  using dasall::contracts::CheckpointState;
  using dasall::runtime::CheckpointStateMapper::can_resume_from;
  using dasall::runtime::CheckpointStateMapper::to_checkpoint_state;
  using dasall::runtime::RuntimeState;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  try {
    constexpr std::array<MappingCase, 17> kMappingCases{{
        {RuntimeState::Idle, CheckpointState::Unspecified},
        {RuntimeState::Receiving, CheckpointState::Running},
        {RuntimeState::Planning, CheckpointState::Running},
        {RuntimeState::Reasoning, CheckpointState::Running},
        {RuntimeState::WaitingClarify, CheckpointState::Paused},
        {RuntimeState::WaitingConfirm, CheckpointState::WaitingConfirm},
        {RuntimeState::ToolCalling, CheckpointState::Running},
        {RuntimeState::WaitingExternal, CheckpointState::WaitingTool},
        {RuntimeState::Reflecting, CheckpointState::Running},
        {RuntimeState::FailedSafe, CheckpointState::Failed},
        {RuntimeState::Responding, CheckpointState::Running},
        {RuntimeState::Auditing, CheckpointState::Running},
        {RuntimeState::Persisting, CheckpointState::Running},
        {RuntimeState::Completed, CheckpointState::Succeeded},
        {RuntimeState::Failed, CheckpointState::Failed},
        {RuntimeState::Degraded, CheckpointState::Failed},
        {RuntimeState::SafeMode, CheckpointState::Failed},
    }};

    for (const auto& mapping_case : kMappingCases) {
      const auto mapped = to_checkpoint_state(mapping_case.runtime_state);
      assert_true(
          mapped.has_value(),
          std::string("known runtime state should map to checkpoint state: ") +
              dasall::runtime::runtime_state_name(mapping_case.runtime_state));
      assert_equal(
          std::string(checkpoint_state_name(mapping_case.checkpoint_state)),
          std::string(checkpoint_state_name(*mapped)),
          std::string("unexpected checkpoint mapping for runtime state ") +
              dasall::runtime::runtime_state_name(mapping_case.runtime_state));
    }

    const auto invalid_runtime_state = static_cast<RuntimeState>(255);
    const auto invalid_mapping = to_checkpoint_state(invalid_runtime_state);
    assert_true(!invalid_mapping.has_value(),
                "invalid runtime state enum value must return nullopt mapping");

    assert_true(can_resume_from(CheckpointState::Running),
                "Running checkpoint should remain resumable");
    assert_true(can_resume_from(CheckpointState::Paused),
                "Paused checkpoint should remain resumable");
    assert_true(can_resume_from(CheckpointState::WaitingConfirm),
                "WaitingConfirm checkpoint should remain resumable");
    assert_true(can_resume_from(CheckpointState::WaitingTool),
                "WaitingTool checkpoint should remain resumable");

    assert_true(!can_resume_from(CheckpointState::Failed),
                "Failed checkpoint should be rejected for resume");
    assert_true(!can_resume_from(CheckpointState::Succeeded),
                "Succeeded checkpoint should be rejected for resume");
    assert_true(!can_resume_from(CheckpointState::Unspecified),
                "Unspecified checkpoint should be rejected for resume");
  } catch (const std::exception& ex) {
    std::cerr << "CheckpointStateMapperTest failed: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}