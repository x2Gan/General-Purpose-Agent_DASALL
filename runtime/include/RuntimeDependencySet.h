#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "checkpoint/Checkpoint.h"
#include "session/SessionTypes.h"

namespace dasall::runtime {

enum class RuntimeStubMainLoopExit : std::uint8_t {
  DirectResponse = 0,
  ToolRound,
  WaitingClarify,
};

enum class RuntimeStubRecoveryExit : std::uint8_t {
  ContinueResponse = 0,
  AbortSafe,
};

struct RuntimeLocalStubPorts {
  bool reject_preflight = false;
  RuntimeStubMainLoopExit main_loop_exit = RuntimeStubMainLoopExit::DirectResponse;
  RuntimeStubRecoveryExit recovery_exit = RuntimeStubRecoveryExit::ContinueResponse;
  std::string success_response_text = "runtime orchestrator skeleton completed";
  std::string fail_safe_response_text = "runtime orchestrator skeleton entered fail-safe";
  std::string waiting_response_text = "runtime orchestrator is waiting for clarification";
};

class RuntimeDependencySet final {
 public:
  RuntimeLocalStubPorts local_stub_ports;
  std::optional<SessionSnapshot> seeded_waiting_session;
  std::vector<contracts::Checkpoint> seeded_checkpoints;
};

}  // namespace dasall::runtime