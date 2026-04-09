#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "checkpoint/RuntimeBudget.h"

namespace dasall::services {

enum class ServiceDataFreshness {
	strict,
	allow_stale,
};

struct CapabilityTargetRef {
	std::string capability_id;
	std::string target_id;
};

struct ServiceCallContext {
	std::string request_id;
	std::string session_id;
	std::string trace_id;
	std::string tool_call_id;
	std::string goal_id;
	std::optional<contracts::RuntimeBudget> budget_guard;
	std::uint64_t deadline_ms;
};

}  // namespace dasall::services