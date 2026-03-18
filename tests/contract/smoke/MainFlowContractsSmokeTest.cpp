#include <exception>
#include <iostream>
#include <type_traits>

#include "agent/MainFlowContracts.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

void test_main_flow_entries_are_fieldless_and_complete() {
  using dasall::contracts::MainFlowContracts;
  using dasall::tests::support::assert_equal;

  // Entries that are still type-only tags (not yet upgraded to full structs).
  static_assert(std::is_empty_v<MainFlowContracts::AgentRequestEntry>);
  static_assert(std::is_empty_v<MainFlowContracts::GoalContractEntry>);
  // ContextPacketEntry upgraded to ContextPacket struct in WP03-T010; no
  // longer empty.  Structural correctness verified by
  // ContextPacketMainFlowContractTest.
  static_assert(!std::is_empty_v<MainFlowContracts::ContextPacketEntry>);
  static_assert(std::is_empty_v<MainFlowContracts::ObservationEntry>);
  static_assert(std::is_empty_v<MainFlowContracts::ObservationDigestEntry>);
  static_assert(std::is_empty_v<MainFlowContracts::BeliefStateEntry>);
  // CheckpointEntry upgraded from empty tag to Checkpoint struct in
  // WP03-T012.  Structural correctness verified by CheckpointContractTest.
  static_assert(!std::is_empty_v<MainFlowContracts::CheckpointEntry>);
  // AgentResultEntry upgraded from empty tag to AgentResult struct in
  // WP03-T014.  Structural correctness verified by AgentResultContractTest.
  static_assert(!std::is_empty_v<MainFlowContracts::AgentResultEntry>);

  assert_equal(8,
               static_cast<int>(MainFlowContracts::canonical_count()),
               "MainFlowContracts should expose exactly 8 canonical objects");
}

void test_main_flow_order_accepts_canonical_successors() {
  using dasall::contracts::MainFlowContracts;
  using dasall::tests::support::assert_true;

  // Positive case: each canonical adjacent pair must be accepted by the order
  // guard so downstream tests can enforce chain stability programmatically.
  assert_true(MainFlowContracts::is_direct_successor(MainFlowContracts::Node::AgentRequest,
                                                     MainFlowContracts::Node::GoalContract),
              "AgentRequest should directly precede GoalContract");
  assert_true(MainFlowContracts::is_direct_successor(MainFlowContracts::Node::GoalContract,
                                                     MainFlowContracts::Node::ContextPacket),
              "GoalContract should directly precede ContextPacket");
  assert_true(MainFlowContracts::is_direct_successor(MainFlowContracts::Node::ContextPacket,
                                                     MainFlowContracts::Node::Observation),
              "ContextPacket should directly precede Observation");
  assert_true(MainFlowContracts::is_direct_successor(MainFlowContracts::Node::Observation,
                                                     MainFlowContracts::Node::ObservationDigest),
              "Observation should directly precede ObservationDigest");
  assert_true(MainFlowContracts::is_direct_successor(
                  MainFlowContracts::Node::ObservationDigest,
                  MainFlowContracts::Node::BeliefState),
              "ObservationDigest should directly precede BeliefState");
  assert_true(MainFlowContracts::is_direct_successor(MainFlowContracts::Node::BeliefState,
                                                     MainFlowContracts::Node::Checkpoint),
              "BeliefState should directly precede Checkpoint");
  assert_true(MainFlowContracts::is_direct_successor(MainFlowContracts::Node::Checkpoint,
                                                     MainFlowContracts::Node::AgentResult),
              "Checkpoint should directly precede AgentResult");
}

void test_main_flow_order_rejects_non_canonical_successors() {
  using dasall::contracts::MainFlowContracts;
  using dasall::tests::support::assert_true;

  // Negative case 1: skipping GoalContract must be rejected.
  assert_true(!MainFlowContracts::is_direct_successor(MainFlowContracts::Node::AgentRequest,
                                                      MainFlowContracts::Node::ContextPacket),
              "AgentRequest cannot directly jump to ContextPacket");

  // Negative case 2: terminal node must not have a direct successor.
  assert_true(!MainFlowContracts::is_direct_successor(MainFlowContracts::Node::AgentResult,
                                                      MainFlowContracts::Node::AgentRequest),
              "AgentResult must not accept any direct successor");
}

}  // namespace

int main() {
  try {
    test_main_flow_entries_are_fieldless_and_complete();
    test_main_flow_order_accepts_canonical_successors();
    test_main_flow_order_rejects_non_canonical_successors();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}