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

namespace dasall::infra {

class IHealthMonitor;
class IHealthProbe;

namespace audit {

class IAuditLogger;

}  // namespace audit

namespace metrics {

class IMetricsProvider;

}  // namespace metrics

namespace tracing {

class ITracerProvider;

}  // namespace tracing

}  // namespace dasall::infra

namespace dasall::multi_agent {

class IMultiAgentCoordinator;

}  // namespace dasall::multi_agent

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

struct RuntimeDependencyReadiness {
  bool has_required_ports = false;
  bool has_optional_ports = false;
  bool degraded = false;
  std::vector<std::string> missing_required_ports;
  std::vector<std::string> missing_optional_ports;

  [[nodiscard]] bool default_unary_ready() const {
    return has_required_ports && has_optional_ports && !degraded;
  }

  [[nodiscard]] std::string readiness_state() const {
    if (!has_required_ports) {
      return "fail_closed";
    }
    if (degraded) {
      return "degraded";
    }
    return "ready";
  }

  [[nodiscard]] std::string summary() const {
    std::string value = readiness_state();
    if (!missing_required_ports.empty()) {
      value += ";missing_required=" + join_ports(missing_required_ports);
    }
    if (!missing_optional_ports.empty()) {
      value += ";missing_optional=" + join_ports(missing_optional_ports);
    }
    return value;
  }

 private:
  [[nodiscard]] static std::string join_ports(const std::vector<std::string>& ports) {
    std::string joined;
    for (std::size_t index = 0; index < ports.size(); ++index) {
      if (index != 0U) {
        joined += ",";
      }
      joined += ports[index];
    }
    return joined;
  }
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
  std::shared_ptr<multi_agent::IMultiAgentCoordinator> multi_agent_coordinator;
  std::shared_ptr<knowledge::IKnowledgeService> knowledge_service;
  std::shared_ptr<llm::ILLMManager> llm_manager;
  std::shared_ptr<infra::audit::IAuditLogger> audit_logger;
  std::shared_ptr<infra::metrics::IMetricsProvider> metrics_provider;
  std::shared_ptr<infra::tracing::ITracerProvider> tracer_provider;
  std::shared_ptr<infra::IHealthMonitor> health_monitor;
  std::vector<std::shared_ptr<infra::IHealthProbe>> health_probes;
  std::vector<std::string> visible_tools;
  std::vector<std::string> external_evidence;

  [[nodiscard]] bool has_live_unary_ports() const {
    return describe_readiness().has_required_ports;
  }

  [[nodiscard]] RuntimeDependencyReadiness describe_readiness() const {
    RuntimeDependencyReadiness readiness;

    if (memory_manager == nullptr) {
      readiness.missing_required_ports.push_back("memory");
    }
    if (cognition_engine == nullptr) {
      readiness.missing_required_ports.push_back("cognition");
    }
    if (response_builder == nullptr) {
      readiness.missing_required_ports.push_back("response_builder");
    }
    if (tool_manager == nullptr) {
      readiness.missing_required_ports.push_back("tools");
    }
    if (knowledge_service == nullptr) {
      readiness.missing_optional_ports.push_back("knowledge");
    }
    if (llm_manager == nullptr) {
      readiness.missing_optional_ports.push_back("llm");
    }

    readiness.has_required_ports = readiness.missing_required_ports.empty();
    readiness.has_optional_ports = readiness.missing_optional_ports.empty();
    readiness.degraded = readiness.has_required_ports && !readiness.has_optional_ports;
    return readiness;
  }
};

}  // namespace dasall::runtime