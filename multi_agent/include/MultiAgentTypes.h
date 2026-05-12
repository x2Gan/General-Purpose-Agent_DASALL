#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "ToolInvocationEnvelope.h"
#include "agent/MultiAgentResult.h"
#include "checkpoint/BudgetSnapshot.h"
#include "checkpoint/Checkpoint.h"
#include "checkpoint/RecoveryRequest.h"
#include "observation/Observation.h"
#include "task/TaskDomainContracts.h"
#include "tool/ToolResult.h"

namespace dasall::multi_agent {

struct MultiAgentExecutionContext {
  std::optional<std::string> runtime_instance_id;
  std::optional<std::string> profile_id;
  std::optional<std::string> trace_id;
  std::optional<std::string> request_id;
  std::optional<std::string> goal_id;
  std::optional<std::string> policy_snapshot_ref;
  std::optional<std::string> parent_checkpoint_ref;
  std::optional<contracts::Checkpoint> checkpoint;
  std::optional<contracts::BudgetSnapshot> runtime_budget_snapshot;
  std::optional<std::uint32_t> retry_count;
  std::optional<contracts::Observation> latest_observation;
  std::vector<contracts::ToolResult> tool_results;
  std::vector<std::string> allowed_tool_domains;
};

struct MultiAgentExecutionReport {
  bool disabled = false;
  std::optional<contracts::MultiAgentResult> multi_agent_result;
  std::optional<contracts::SubTaskGraph> graph_snapshot;
  std::vector<contracts::Observation> emitted_observations;
  std::vector<tools::ToolCompensationHint> compensation_hints;
  std::optional<contracts::RecoveryRequest> recovery_request;
  std::vector<std::string> audit_refs;
};

struct MultiAgentRuntimeFoldResult {
  std::optional<contracts::Observation> latest_observation;
  std::vector<tools::ToolCompensationHint> compensation_hints;
  std::optional<contracts::RecoveryRequest> recovery_request;
  std::vector<std::string> audit_refs;
};

}  // namespace dasall::multi_agent