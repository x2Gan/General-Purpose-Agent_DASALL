#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "RuntimePolicySnapshot.h"

namespace dasall::runtime {

class RuntimeDependencySet;

enum class AgentInitReadinessLevel : std::uint8_t {
  Rejected = 0,
  StubReady,
  DegradedReady,
  DefaultReady,
};

[[nodiscard]] inline const char* to_string(const AgentInitReadinessLevel level) {
  switch (level) {
    case AgentInitReadinessLevel::Rejected:
      return "rejected";
    case AgentInitReadinessLevel::StubReady:
      return "stub-ready";
    case AgentInitReadinessLevel::DegradedReady:
      return "degraded-ready";
    case AgentInitReadinessLevel::DefaultReady:
      return "default-ready";
  }

  return "rejected";
}

struct AgentInitRequest {
  std::string runtime_instance_id;
  std::string profile_id;
  std::shared_ptr<const profiles::RuntimePolicySnapshot> policy_snapshot;
  std::shared_ptr<RuntimeDependencySet> dependency_set;
  std::string boot_reason;
  bool cold_start = true;

  [[nodiscard]] bool has_minimum_requirements() const {
    return !runtime_instance_id.empty() && !profile_id.empty() && policy_snapshot != nullptr &&
           dependency_set != nullptr;
  }
};

struct AgentInitResult {
  bool accepted = false;
  std::string runtime_instance_id;
  std::string resolved_profile_id;
  bool degraded = false;
  std::vector<std::string> missing_required_ports;
  std::vector<std::string> missing_optional_ports;
  std::vector<std::string> degraded_reasons;
  std::optional<AgentInitReadinessLevel> projected_readiness;
  std::string health_summary;
  std::int32_t error_code = 0;
  std::string diagnostics;

  [[nodiscard]] AgentInitReadinessLevel readiness_level() const {
    if (!accepted) {
      return AgentInitReadinessLevel::Rejected;
    }

    if (projected_readiness.has_value()) {
      return *projected_readiness;
    }

    if (diagnostics.find("entrypoint_ready=stub-ready") != std::string::npos ||
        diagnostics.find("readiness=stub_runtime_path") != std::string::npos) {
      return AgentInitReadinessLevel::StubReady;
    }

    if (degraded ||
        diagnostics.find("entrypoint_ready=degraded-ready") != std::string::npos) {
      return AgentInitReadinessLevel::DegradedReady;
    }

    return AgentInitReadinessLevel::DefaultReady;
  }

  [[nodiscard]] bool stub_ready() const {
    return readiness_level() == AgentInitReadinessLevel::StubReady;
  }

  [[nodiscard]] bool degraded_ready() const {
    return readiness_level() == AgentInitReadinessLevel::DegradedReady;
  }

  [[nodiscard]] bool default_ready() const {
    return readiness_level() == AgentInitReadinessLevel::DefaultReady;
  }

  [[nodiscard]] std::string readiness_label() const {
    return to_string(readiness_level());
  }

  [[nodiscard]] bool is_ready() const {
    return accepted;
  }
};

struct HandleOptions {
  std::string request_id;
  std::string session_id;
  std::string caller_id;
  std::string entrypoint;
  std::optional<std::string> checkpoint_ref;
  std::optional<std::uint32_t> timeout_override_ms;
  std::string trace_context;
};

[[nodiscard]] inline std::string make_resume_binding_token(
    const std::string& session_id,
    const std::string& checkpoint_ref) {
  return std::string("resume-bind:") + session_id + ":" + checkpoint_ref;
}

struct ResumeHandleRequest {
  std::string request_id;
  std::string session_id;
  std::string checkpoint_ref;
  std::string resume_reason;
  std::string resume_token;
  std::string trace_context;
  std::optional<HandleOptions> override_options;

  [[nodiscard]] bool has_minimum_requirements() const {
    return !request_id.empty() && !session_id.empty() && !checkpoint_ref.empty() &&
           !resume_reason.empty() && !resume_token.empty();
  }
};

}  // namespace dasall::runtime