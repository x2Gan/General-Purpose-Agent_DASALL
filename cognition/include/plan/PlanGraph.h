#pragma once

// Schema baseline: cognition.plan.v1.

#include <cstdint>
#include <string>
#include <vector>

namespace dasall::cognition::plan {

struct PlanNode {
  std::string node_id;
  std::string objective;
  std::string success_signal;
  std::string action_kind_hint;
  std::vector<std::string> depends_on;
  std::vector<std::string> evidence_refs;
};

struct PlanEdge {
  std::string from_node_id;
  std::string to_node_id;
  std::string condition;
  std::vector<std::string> evidence_refs;
};

struct PlanOpenQuestion {
  std::string question_id;
  std::string question;
  std::string reason;
  bool blocks_plan = true;
  std::vector<std::string> evidence_refs;
};

struct PlanGraph {
  std::string plan_id;
  std::uint32_t revision = 0;
  std::vector<PlanNode> nodes;
  std::vector<PlanEdge> edges;
  std::vector<PlanOpenQuestion> open_questions;
  std::string plan_rationale;
  std::uint32_t estimated_complexity = 0;
};

}  // namespace dasall::cognition::plan
