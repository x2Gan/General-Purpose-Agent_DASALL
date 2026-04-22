#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "AgentTypes.h"
#include "agent/AgentRequest.h"
#include "agent/AgentResult.h"
#include "budget/BudgetController.h"
#include "checkpoint/Checkpoint.h"
#include "checkpoint/CheckpointManager.h"
#include "checkpoint/RecoveryOutcome.h"
#include "checkpoint/RuntimeBudget.h"
#include "fsm/IAgentFsm.h"
#include "fsm/StateTransitionTypes.h"
#include "recovery/RecoveryManager.h"
#include "recovery/ResumePlan.h"
#include "scheduling/Scheduler.h"
#include "scheduling/SchedulerTicket.h"
#include "session/SessionManager.h"
#include "session/SessionTypes.h"

namespace dasall::runtime {

enum class StubMainLoopExit : std::uint8_t {
  DirectResponse = 0,
  ToolRound,
  WaitingClarify,
};

enum class StubRecoveryExit : std::uint8_t {
  ContinueResponse = 0,
  AbortSafe,
};

struct OrchestratorStubPorts {
  bool reject_preflight = false;
  StubMainLoopExit main_loop_exit = StubMainLoopExit::DirectResponse;
  StubRecoveryExit recovery_exit = StubRecoveryExit::ContinueResponse;
  std::string success_response_text = "runtime orchestrator skeleton completed";
  std::string fail_safe_response_text = "runtime orchestrator skeleton entered fail-safe";
  std::string waiting_response_text = "runtime orchestrator is waiting for clarification";
};

enum class OrchestratorStage : std::uint8_t {
  Preflight = 0,
  MainLoop,
  ToolRound,
  RecoveryRound,
  Terminalize,
};

[[nodiscard]] const char* orchestrator_stage_name(OrchestratorStage stage);

struct OrchestratorStageTrace {
  OrchestratorStage stage = OrchestratorStage::Preflight;
  RuntimeState state_before = RuntimeState::Idle;
  RuntimeState state_after = RuntimeState::Idle;
  bool entered = false;
  std::string detail;
};

struct OrchestratorRunResult {
  contracts::AgentResult agent_result;
  std::vector<OrchestratorStageTrace> stage_trace;
  RuntimeState final_state = RuntimeState::Idle;
  bool used_tool_round = false;
  bool used_recovery_round = false;
  std::optional<SessionSnapshot> effective_session;
  std::optional<contracts::Checkpoint> checkpoint;
  std::optional<contracts::RecoveryOutcome> recovery_outcome;
  std::optional<SchedulerBackpressureState> scheduler_backpressure;
  std::optional<ResumePlan> resume_plan;
};

struct OrchestratorComposition {
  using FsmFactory = std::function<std::unique_ptr<IAgentFsm>(RuntimeState)>;

  std::string runtime_instance_id;
  std::string profile_id;
  std::shared_ptr<const profiles::RuntimePolicySnapshot> policy_snapshot;
  std::shared_ptr<RuntimeDependencySet> dependency_set;
  OrchestratorStubPorts stub_ports;
  FsmFactory fsm_factory;
  contracts::RuntimeBudget default_runtime_budget{
      .max_tokens = 1000,
      .max_turns = 4,
      .max_tool_calls = 1,
      .max_latency_ms = 1500,
      .max_replan_count = 2,
  };
  std::uint64_t budget_started_at_ms = 42;
  std::string default_goal_id = "goal-001";
  std::string default_belief_state_ref = "belief-001";
  std::string default_policy_snapshot_ref = "policy-001";
  std::string default_worker_id = "worker-001";
  std::string default_audit_summary = "runtime-local orchestrator assembly";
  std::string waiting_prompt_token = "prompt-001";
  std::string waiting_resume_channel = "user_reply";
  std::string waiting_input_schema_hint = "text/plain";
};

class AgentOrchestrator final {
 public:
  explicit AgentOrchestrator(OrchestratorComposition composition = {});

  AgentOrchestrator(const AgentOrchestrator&) = delete;
  AgentOrchestrator& operator=(const AgentOrchestrator&) = delete;

  [[nodiscard]] OrchestratorRunResult run_once(const contracts::AgentRequest& request);
  [[nodiscard]] OrchestratorRunResult continue_from_checkpoint(
      const ResumePlan& plan,
      const SessionSnapshot& session_snapshot);
  [[nodiscard]] OrchestratorRunResult handle_waiting_state(
      const SessionSnapshot& session_snapshot,
      const ResumeHandleRequest& request);

 private:
  [[nodiscard]] std::unique_ptr<IAgentFsm> build_fsm(RuntimeState initial_state) const;

  OrchestratorComposition composition_;
  BudgetController budget_controller_;
  CheckpointManager checkpoint_manager_;
  RecoveryManager recovery_manager_;
  Scheduler scheduler_;
  SessionManager session_manager_;
  std::uint64_t next_sequence_ = 1;
};

}  // namespace dasall::runtime