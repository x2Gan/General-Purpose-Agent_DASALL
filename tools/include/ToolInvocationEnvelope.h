#pragma once

#include <optional>
#include <string>
#include <vector>

#include "observation/Observation.h"
#include "observation/ObservationDigest.h"
#include "tool/ToolResult.h"

namespace dasall::tools {

struct ToolRouteFacts {
	std::optional<std::string> route_kind;
	std::optional<std::string> route_ref;
	std::optional<std::string> decision_reason;
	std::optional<std::string> plugin_id;
	std::optional<std::string> server_id;
};

struct ToolCompensationHint {
	std::optional<std::string> compensation_action;
	std::optional<std::string> target_ref;
	std::optional<std::string> reason_code;
	std::optional<std::vector<std::string>> evidence_refs;
};

// Unified runtime-facing return surface. Shared execution and projection
// objects stay embedded as-is, while route facts, evidence references, and
// compensation hints remain module-local supporting data.
struct ToolInvocationEnvelope {
	std::optional<dasall::contracts::ToolResult> tool_result;
	std::optional<dasall::contracts::Observation> observation;
	std::optional<dasall::contracts::ObservationDigest> observation_digest;
	std::optional<ToolRouteFacts> route_facts;
	std::optional<std::vector<std::string>> evidence_refs;
	std::optional<std::vector<ToolCompensationHint>> compensation_hints;
	std::optional<std::string> failure_reason_code;

	[[nodiscard]] bool has_projection() const {
		return observation.has_value() && observation_digest.has_value();
	}
};

}  // namespace dasall::tools