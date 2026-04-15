#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include "ToolInvocationEnvelope.h"

namespace {

using dasall::tools::ToolCompensationHint;
using dasall::tools::ToolInvocationEnvelope;
using dasall::tools::ToolRouteFacts;

static_assert(std::is_same_v<decltype(ToolRouteFacts{}.route_kind),
                             std::optional<std::string>>);
static_assert(std::is_same_v<decltype(ToolRouteFacts{}.route_ref),
                             std::optional<std::string>>);
static_assert(std::is_same_v<decltype(ToolRouteFacts{}.decision_reason),
                             std::optional<std::string>>);
static_assert(std::is_same_v<decltype(ToolRouteFacts{}.plugin_id),
                             std::optional<std::string>>);
static_assert(std::is_same_v<decltype(ToolRouteFacts{}.server_id),
                             std::optional<std::string>>);

static_assert(std::is_same_v<decltype(ToolCompensationHint{}.compensation_action),
                             std::optional<std::string>>);
static_assert(std::is_same_v<decltype(ToolCompensationHint{}.target_ref),
                             std::optional<std::string>>);
static_assert(std::is_same_v<decltype(ToolCompensationHint{}.reason_code),
                             std::optional<std::string>>);
static_assert(std::is_same_v<decltype(ToolCompensationHint{}.evidence_refs),
                             std::optional<std::vector<std::string>>>);

static_assert(std::is_same_v<decltype(ToolInvocationEnvelope{}.tool_result),
                             std::optional<dasall::contracts::ToolResult>>);
static_assert(std::is_same_v<decltype(ToolInvocationEnvelope{}.observation),
                             std::optional<dasall::contracts::Observation>>);
static_assert(std::is_same_v<decltype(ToolInvocationEnvelope{}.observation_digest),
                             std::optional<dasall::contracts::ObservationDigest>>);
static_assert(std::is_same_v<decltype(ToolInvocationEnvelope{}.route_facts),
                             std::optional<ToolRouteFacts>>);
static_assert(std::is_same_v<decltype(ToolInvocationEnvelope{}.evidence_refs),
                             std::optional<std::vector<std::string>>>);
static_assert(std::is_same_v<decltype(ToolInvocationEnvelope{}.compensation_hints),
                             std::optional<std::vector<ToolCompensationHint>>>);
static_assert(std::is_same_v<decltype(ToolInvocationEnvelope{}.failure_reason_code),
                             std::optional<std::string>>);

void tool_invocation_envelope_surface_keeps_shared_results_and_module_local_facts() {
  dasall::contracts::ToolResult tool_result;
  tool_result.tool_call_id = std::string("tool-call-003");
  tool_result.tool_name = std::string("agent.terminal");
  tool_result.success = true;
  tool_result.payload = std::string("{\"exit_code\":0}");

  dasall::contracts::Observation observation;
  observation.observation_id = std::string("obs-tools-003");
  observation.success = true;
  observation.payload = tool_result.payload;

  dasall::contracts::ObservationDigest digest;
  digest.observation_id = observation.observation_id;
  digest.summary = std::string("terminal command completed");
  digest.key_facts = std::vector<std::string>{"exit_code=0"};
  digest.citations = std::vector<std::string>{"artifact://terminal/003"};
  digest.confidence = 0.9F;

  const ToolInvocationEnvelope envelope{
    .tool_result = tool_result,
    .observation = observation,
    .observation_digest = digest,
    .route_facts = ToolRouteFacts{
      .route_kind = std::string("builtin"),
      .route_ref = std::string("builtin:agent.terminal"),
      .decision_reason = std::string("local-capability"),
      .plugin_id = std::nullopt,
      .server_id = std::nullopt,
    },
    .evidence_refs = std::vector<std::string>{"artifact://terminal/003"},
    .compensation_hints = std::vector<ToolCompensationHint>{
      ToolCompensationHint{
        .compensation_action = std::string("cleanup.tempfile"),
        .target_ref = std::string("tmp://agent-terminal-003"),
        .reason_code = std::string("PartialSideEffect"),
        .evidence_refs = std::vector<std::string>{"artifact://terminal/003"},
      },
    },
    .failure_reason_code = std::nullopt,
  };

  static_cast<void>(envelope);
}

}  // namespace