#pragma once

#include <array>
#include <cstddef>

#include "agent/AgentRequestTag.h"
#include "agent/AgentResult.h"
#include "agent/AgentResultTag.h"
#include "agent/GoalContractTag.h"
#include "checkpoint/Checkpoint.h"
#include "checkpoint/CheckpointTag.h"
#include "context/ContextPacket.h"
#include "context/ContextPacketTag.h"
#include "observation/ObservationDigestTag.h"
#include "observation/ObservationTag.h"

#include "agent/BeliefStateTag.h"

namespace dasall::contracts {

// MainFlowContracts centralizes the WP03-T001 stable object chain entrypoints.
// This header intentionally exposes only object-level markers and ordering
// guards, so the design remains in object-boundary scope without freezing
// field-level schema details ahead of downstream work packages.
struct MainFlowContracts final {
  enum class Node {
    AgentRequest = 0,
    GoalContract = 1,
    ContextPacket = 2,
    Observation = 3,
    ObservationDigest = 4,
    BeliefState = 5,
    Checkpoint = 6,
    AgentResult = 7,
  };

  using AgentRequestEntry = AgentRequestTag;
  using GoalContractEntry = GoalContractTag;
  // ContextPacket contract object introduced in WP03-T010.
  // Full struct definition available in context/ContextPacket.h.
  // Tag retained for backward compatibility with WP-01 smoke tests.
  using ContextPacketEntry = ContextPacket;
  using ObservationEntry = ObservationTag;
  using ObservationDigestEntry = ObservationDigestTag;

  // BeliefState contract object introduced in WP03-T009.
  // Tag occupies the chain slot; the full BeliefState struct is in
  // agent/BeliefState.h (separate include, same as other entries).
  using BeliefStateEntry = BeliefStateTag;

  // Checkpoint contract object introduced in WP03-T012.
  // Full struct definition available in checkpoint/Checkpoint.h.
  // Tag retained for backward compatibility with WP-01 smoke tests.
  using CheckpointEntry = Checkpoint;
  // AgentResult contract object introduced in WP03-T014.
  // Full struct definition available in agent/AgentResult.h.
  // Tag retained for backward compatibility with WP-01 smoke tests.
  using AgentResultEntry = AgentResult;

  static constexpr std::array<Node, 8> kCanonicalOrder = {
      Node::AgentRequest,
      Node::GoalContract,
      Node::ContextPacket,
      Node::Observation,
      Node::ObservationDigest,
      Node::BeliefState,
      Node::Checkpoint,
      Node::AgentResult,
  };

  static constexpr int index_of(Node node) {
    return static_cast<int>(node);
  }

  static constexpr bool is_direct_successor(Node current, Node candidate) {
    const int current_index = index_of(current);
    const int candidate_index = index_of(candidate);
    return candidate_index == current_index + 1;
  }

  static constexpr std::size_t canonical_count() {
    return kCanonicalOrder.size();
  }
};

}  // namespace dasall::contracts