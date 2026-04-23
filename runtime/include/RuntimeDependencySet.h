#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "checkpoint/Checkpoint.h"
#include "session/SessionTypes.h"

namespace dasall::cognition {

class ICognitionEngine;
class IResponseBuilder;

}  // namespace dasall::cognition

namespace dasall::knowledge {

class IKnowledgeService;

}  // namespace dasall::knowledge

namespace dasall::llm {

class ILLMManager;

}  // namespace dasall::llm

namespace dasall::memory {

class IMemoryManager;

}  // namespace dasall::memory

namespace dasall::tools {

class IToolManager;

}  // namespace dasall::tools

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
  std::shared_ptr<memory::IMemoryManager> memory_manager;
  std::shared_ptr<cognition::ICognitionEngine> cognition_engine;
  std::shared_ptr<cognition::IResponseBuilder> response_builder;
  std::shared_ptr<tools::IToolManager> tool_manager;
  std::shared_ptr<knowledge::IKnowledgeService> knowledge_service;
  std::shared_ptr<llm::ILLMManager> llm_manager;
  std::vector<std::string> visible_tools;
  std::vector<std::string> external_evidence;

  [[nodiscard]] bool has_live_unary_ports() const {
    return memory_manager != nullptr && cognition_engine != nullptr &&
           response_builder != nullptr && tool_manager != nullptr;
  }
};

}  // namespace dasall::runtime