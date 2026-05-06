#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "checkpoint/RuntimeBudget.h"
#include "error/ErrorInfo.h"
#include "error/ResultCode.h"

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

[[nodiscard]] inline std::optional<contracts::ResultCode> service_result_effective_failure_code(
		const std::optional<contracts::ResultCode>& code,
		const std::optional<contracts::ErrorInfo>& error) {
	if (code.has_value()) {
		return code;
	}

	if (!error.has_value() || !error->details.code.has_value()) {
		return std::nullopt;
	}

	const auto classification =
			contracts::classify_result_code_value(*error->details.code);
	if (!classification.ok) {
		return std::nullopt;
	}

	return static_cast<contracts::ResultCode>(*error->details.code);
}

[[nodiscard]] inline bool service_result_has_consistent_triad(
		const std::optional<contracts::ResultCode>& code,
		const std::optional<contracts::ErrorInfo>& error) {
	if (code.has_value() != error.has_value()) {
		return false;
	}

	if (!error.has_value()) {
		return true;
	}

	if (error->failure_type.has_value() &&
			contracts::classify_result_code(*code) != *error->failure_type) {
		return false;
	}

	if (error->details.code.has_value() &&
			*error->details.code != static_cast<int>(*code)) {
		return false;
	}

	return true;
}

[[nodiscard]] inline bool service_result_succeeded(
		const std::optional<contracts::ResultCode>& code,
		const std::optional<contracts::ErrorInfo>& error) {
	return !code.has_value() && !error.has_value();
}

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

struct ExecutionCommandResult {
	std::optional<contracts::ResultCode> code;
	std::string execution_id;
	SerializedJson payload_json;
	std::vector<std::string> side_effects;
	std::vector<std::string> compensation_hints;
	std::optional<contracts::ErrorInfo> error;

	[[nodiscard]] bool has_consistent_values() const {
		return service_result_has_consistent_triad(code, error);
	}

	[[nodiscard]] bool succeeded() const {
		return service_result_succeeded(code, error);
	}
};

struct ExecutionQueryResult {
	std::optional<contracts::ResultCode> code;
	std::string state;
	SerializedJson snapshot_json;
	bool from_cache;
	std::optional<contracts::ErrorInfo> error;

	[[nodiscard]] bool has_consistent_values() const {
		return service_result_has_consistent_triad(code, error);
	}

	[[nodiscard]] bool succeeded() const {
		return service_result_succeeded(code, error);
	}
};

struct ExecutionSubscriptionResult {
	std::optional<contracts::ResultCode> code;
	SerializedJson events_json;
	std::optional<std::string> next_cursor;
	bool resync_required;
	std::uint32_t dropped_count;
	std::optional<contracts::ErrorInfo> error;

	[[nodiscard]] bool has_consistent_values() const {
		return service_result_has_consistent_triad(code, error);
	}

	[[nodiscard]] bool succeeded() const {
		return service_result_succeeded(code, error);
	}
};

struct ExecutionDiagnoseResult {
	std::optional<contracts::ResultCode> code;
	bool target_reachable;
	SerializedJson report_json;
	std::optional<contracts::ErrorInfo> error;

	[[nodiscard]] bool has_consistent_values() const {
		return service_result_has_consistent_triad(code, error);
	}

	[[nodiscard]] bool succeeded() const {
		return service_result_succeeded(code, error);
	}
};

struct DataQueryRequest {
	ServiceCallContext context;
	std::string dataset;
	SerializedJson filters_json;
	std::string projection;
	ServiceDataFreshness freshness;
};

struct DataCatalogRequest {
	ServiceCallContext context;
	std::string target_class;
};

struct DataQueryResult {
	std::optional<contracts::ResultCode> code;
	SerializedJson rows_json;
	bool from_cache;
	std::optional<contracts::ErrorInfo> error;

	[[nodiscard]] bool has_consistent_values() const {
		return service_result_has_consistent_triad(code, error);
	}

	[[nodiscard]] bool succeeded() const {
		return service_result_succeeded(code, error);
	}
};

struct DataCatalogResult {
	std::optional<contracts::ResultCode> code;
	SerializedJson catalog_json;
	std::optional<contracts::ErrorInfo> error;

	[[nodiscard]] bool has_consistent_values() const {
		return service_result_has_consistent_triad(code, error);
	}

	[[nodiscard]] bool succeeded() const {
		return service_result_succeeded(code, error);
	}
};

}  // namespace dasall::services