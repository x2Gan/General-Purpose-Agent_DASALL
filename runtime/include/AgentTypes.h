#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "RuntimePolicySnapshot.h"

namespace dasall::runtime {

class RuntimeDependencySet;

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
  std::string health_summary;
  std::int32_t error_code = 0;
  std::string diagnostics;

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