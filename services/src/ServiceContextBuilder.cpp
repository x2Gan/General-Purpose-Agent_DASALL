#include "ServiceContextBuilder.h"

#include <utility>

namespace dasall::services::internal {

namespace {

[[nodiscard]] ContextNormalizationResult missing_field_result(const std::string& field_name) {
	return ContextNormalizationResult{
			.context = std::nullopt,
			.error = field_name + " is required",
	};
}

}  // namespace

ContextNormalizationResult ServiceContextBuilder::normalize_context(
		const ServiceCallContext& candidate) const {
	if (candidate.request_id.empty()) {
		return missing_field_result("request_id");
	}

	if (candidate.session_id.empty()) {
		return missing_field_result("session_id");
	}

	if (candidate.trace_id.empty()) {
		return missing_field_result("trace_id");
	}

	if (candidate.tool_call_id.empty()) {
		return missing_field_result("tool_call_id");
	}

	if (candidate.goal_id.empty()) {
		return missing_field_result("goal_id");
	}

	if (candidate.deadline_ms == 0U) {
		return ContextNormalizationResult{
				.context = std::nullopt,
				.error = "deadline_ms must be greater than 0",
		};
	}

	return ContextNormalizationResult{
			.context = candidate,
			.error = {},
	};
}

}  // namespace dasall::services::internal