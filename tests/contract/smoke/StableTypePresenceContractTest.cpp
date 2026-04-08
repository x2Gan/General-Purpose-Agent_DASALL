#include <array>
#include <exception>
#include <iostream>
#include <type_traits>

#include "agent/ActionDecisionTag.h"
#include "agent/AgentRequestTag.h"
#include "agent/AgentResultTag.h"
#include "agent/GoalContractTag.h"
#include "agent/MultiAgentRequestTag.h"
#include "agent/MultiAgentResultTag.h"
#include "checkpoint/CheckpointTag.h"
#include "checkpoint/RecoveryOutcomeTag.h"
#include "checkpoint/ReflectionDecisionTag.h"
#include "context/ContextPacketTag.h"
#include "boundary/ObjectBoundaryCatalog.h"
#include "support/TestAssertions.h"
#include "observation/ErrorInfoTag.h"
#include "observation/ObservationDigestTag.h"
#include "observation/ObservationTag.h"
#include "task/WorkerTaskTag.h"

namespace {

void test_all_stable_tags_are_includeable_and_fieldless() {
  using dasall::contracts::ActionDecisionTag;
  using dasall::contracts::AgentRequestTag;
  using dasall::contracts::AgentResultTag;
  using dasall::contracts::CheckpointTag;
  using dasall::contracts::ContextPacketTag;
  using dasall::contracts::ErrorInfoTag;
  using dasall::contracts::GoalContractTag;
  using dasall::contracts::MultiAgentRequestTag;
  using dasall::contracts::MultiAgentResultTag;
  using dasall::contracts::ObservationDigestTag;
  using dasall::contracts::ObservationTag;
  using dasall::contracts::RecoveryOutcomeTag;
  using dasall::contracts::ReflectionDecisionTag;
  using dasall::contracts::WorkerTaskTag;
  using dasall::tests::support::assert_equal;

  // The 14 stable objects in WP01-B002 must all have include-able tag types.
  constexpr std::size_t kStableTagCount = 14;
  assert_equal(14,
               static_cast<int>(kStableTagCount),
               "stable tag collection should contain 14 placeholders");

  // Every placeholder must remain empty to avoid freezing field semantics.
  static_assert(std::is_empty_v<AgentRequestTag>);
  static_assert(std::is_empty_v<GoalContractTag>);
  static_assert(std::is_empty_v<ContextPacketTag>);
  static_assert(std::is_empty_v<ActionDecisionTag>);
  static_assert(std::is_empty_v<ObservationTag>);
  static_assert(std::is_empty_v<WorkerTaskTag>);
  static_assert(std::is_empty_v<ObservationDigestTag>);
  static_assert(std::is_empty_v<ErrorInfoTag>);
  static_assert(std::is_empty_v<CheckpointTag>);
  static_assert(std::is_empty_v<ReflectionDecisionTag>);
  static_assert(std::is_empty_v<RecoveryOutcomeTag>);
  static_assert(std::is_empty_v<AgentResultTag>);
  static_assert(std::is_empty_v<MultiAgentRequestTag>);
  static_assert(std::is_empty_v<MultiAgentResultTag>);
}

void test_stable_catalog_entries_match_wp01_stable_objects() {
  using dasall::contracts::ContractObject;
  using dasall::contracts::count_by_category;
  using dasall::contracts::is_stable_object;
  using dasall::contracts::BoundaryCategory;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  constexpr std::array<ContractObject, 14> kStableObjects = {
      ContractObject::AgentRequest,
      ContractObject::GoalContract,
      ContractObject::ContextPacket,
      ContractObject::ActionDecision,
      ContractObject::Observation,
      ContractObject::WorkerTask,
      ContractObject::ObservationDigest,
      ContractObject::ErrorInfo,
      ContractObject::Checkpoint,
      ContractObject::ReflectionDecision,
      ContractObject::RecoveryOutcome,
      ContractObject::AgentResult,
      ContractObject::MultiAgentRequest,
      ContractObject::MultiAgentResult,
  };

  assert_equal(14,
               static_cast<int>(count_by_category(BoundaryCategory::Stable)),
               "WP01 stable category count should remain 14");

  for (const auto object : kStableObjects) {
    assert_true(is_stable_object(object),
                "all stable placeholders must map to stable catalog entries");
  }
}

void test_non_stable_objects_are_not_accidentally_promoted() {
  using dasall::contracts::ContractObject;
  using dasall::contracts::is_stable_object;
  using dasall::tests::support::assert_true;

  // Negative case: blocked object must never be considered stable.
  assert_true(!is_stable_object(ContractObject::MemoryEvidence),
              "MemoryEvidence must remain non-stable in WP01");

  // Negative case: deferred object must not be promoted to stable.
  assert_true(!is_stable_object(ContractObject::ToolRequest),
              "ToolRequest must remain deferred in WP01");
}

}  // namespace

int main() {
  try {
    test_all_stable_tags_are_includeable_and_fieldless();
    test_stable_catalog_entries_match_wp01_stable_objects();
    test_non_stable_objects_are_not_accidentally_promoted();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
