#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "AgentTypes.h"
#include "agent/AgentRequest.h"
#include "agent/AgentResult.h"
#include "fsm/IAgentFsm.h"
#include "fsm/StateTransitionTypes.h"

namespace dasall::runtime {

enum class StubMainLoopExit : std::uint8_t {
  DirectResponse = 0,
  ToolRound,
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
};

struct OrchestratorComposition {
  using FsmFactory = std::function<std::unique_ptr<IAgentFsm>()>;

  std::string runtime_instance_id;
  std::string profile_id;
  std::shared_ptr<const profiles::RuntimePolicySnapshot> policy_snapshot;
  std::shared_ptr<RuntimeDependencySet> dependency_set;
  OrchestratorStubPorts stub_ports;
  FsmFactory fsm_factory;
};

class AgentOrchestrator final {
 public:
  explicit AgentOrchestrator(OrchestratorComposition composition = {});

  AgentOrchestrator(const AgentOrchestrator&) = delete;
  AgentOrchestrator& operator=(const AgentOrchestrator&) = delete;

  [[nodiscard]] OrchestratorRunResult run_once(const contracts::AgentRequest& request);

 private:
  [[nodiscard]] std::unique_ptr<IAgentFsm> build_fsm() const;

  OrchestratorComposition composition_;
};

}  // namespace dasall::runtime