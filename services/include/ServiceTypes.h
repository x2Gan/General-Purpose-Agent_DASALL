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

using SerializedJson = std::string;

struct ExecutionCommandRequest {
	ServiceCallContext context;
	CapabilityTargetRef target;
	std::string action;
	SerializedJson arguments_json;
	std::optional<std::string> idempotency_key;
};

struct ExecutionCompensationRequest {
	ServiceCallContext context;
	CapabilityTargetRef target;
	std::string compensation_action;
	SerializedJson arguments_json;
	std::string source_execution_id;
	std::string reason_code;
};

struct ExecutionQueryRequest {
	ServiceCallContext context;
	CapabilityTargetRef target;
	std::string query_kind;
	ServiceDataFreshness freshness;
};

struct ExecutionSubscriptionRequest {
	ServiceCallContext context;
	CapabilityTargetRef target;
	std::string stream_kind;
	std::optional<std::string> cursor;
	std::uint32_t max_events;
};

struct ExecutionDiagnoseRequest {
	ServiceCallContext context;
	CapabilityTargetRef target;
	bool include_last_error;
};

}  // namespace dasall::services