#include "ServiceFacade.h"

#include <utility>

namespace dasall::services::internal {

namespace {

[[nodiscard]] contracts::ErrorInfo make_error_info(contracts::ResultCode code,
																									 contracts::ResultCodeCategory category,
																									 bool retryable,
																									 bool safe_to_replan,
																									 const std::string& message,
																									 const std::string& stage,
																									 const std::string& ref_id) {
	return contracts::ErrorInfo{
			.failure_type = category,
			.retryable = retryable,
			.safe_to_replan = safe_to_replan,
			.details = {
					.code = static_cast<int>(code),
					.message = message,
					.stage = stage,
			},
			.source_ref = {
					.ref_type = "services",
					.ref_id = ref_id,
			},
	};
}

[[nodiscard]] ExecutionCommandResult make_command_failure(contracts::ResultCode code,
																													contracts::ResultCodeCategory category,
																													const std::string& message,
																													const std::string& stage,
																													const std::string& ref_id) {
	return ExecutionCommandResult{
			.code = code,
			.execution_id = {},
			.payload_json = {},
			.side_effects = {},
			.compensation_hints = {},
			.error = make_error_info(code, category, false, false, message, stage, ref_id),
	};
}

[[nodiscard]] ExecutionQueryResult make_query_failure(contracts::ResultCode code,
																											contracts::ResultCodeCategory category,
																											const std::string& message,
																											const std::string& stage,
																											const std::string& ref_id) {
	return ExecutionQueryResult{
			.code = code,
			.state = {},
			.snapshot_json = {},
			.from_cache = false,
			.error = make_error_info(code, category, false, false, message, stage, ref_id),
	};
}

[[nodiscard]] ExecutionSubscriptionResult make_subscription_failure(
		contracts::ResultCode code,
		contracts::ResultCodeCategory category,
		const std::string& message,
		const std::string& stage,
		const std::string& ref_id) {
	return ExecutionSubscriptionResult{
			.code = code,
			.events_json = {},
			.next_cursor = std::nullopt,
			.resync_required = false,
			.dropped_count = 0U,
			.error = make_error_info(code, category, false, false, message, stage, ref_id),
	};
}

[[nodiscard]] ExecutionDiagnoseResult make_diagnose_failure(contracts::ResultCode code,
																														contracts::ResultCodeCategory category,
																														const std::string& message,
																														const std::string& stage,
																														const std::string& ref_id) {
	return ExecutionDiagnoseResult{
			.code = code,
			.target_reachable = false,
			.report_json = {},
			.error = make_error_info(code, category, false, false, message, stage, ref_id),
	};
}

[[nodiscard]] DataQueryResult make_data_query_failure(contracts::ResultCode code,
																											contracts::ResultCodeCategory category,
																											const std::string& message,
																											const std::string& stage,
																											const std::string& ref_id) {
	return DataQueryResult{
			.code = code,
			.rows_json = {},
			.from_cache = false,
			.error = make_error_info(code, category, false, false, message, stage, ref_id),
	};
}

[[nodiscard]] DataCatalogResult make_data_catalog_failure(contracts::ResultCode code,
																													contracts::ResultCodeCategory category,
																													const std::string& message,
																													const std::string& stage,
																													const std::string& ref_id) {
	return DataCatalogResult{
			.code = code,
			.catalog_json = {},
			.error = make_error_info(code, category, false, false, message, stage, ref_id),
	};
}

}  // namespace

ServiceFacade::ServiceFacade(ServiceFacadeDependencies dependencies)
		: dependencies_(std::move(dependencies)) {}

ExecutionCommandResult ServiceFacade::execute(const ExecutionCommandRequest& request) {
	if (dependencies_.context_builder == nullptr) {
		return make_command_failure(contracts::ResultCode::RuntimeRetryExhausted,
																contracts::ResultCodeCategory::Runtime,
																"service context builder is not configured",
																"service_facade",
																"execute");
	}

	const auto normalized = dependencies_.context_builder->normalize_context(request.context);
	if (!normalized.ok()) {
		return make_command_failure(contracts::ResultCode::ValidationFieldMissing,
																contracts::ResultCodeCategory::Validation,
																normalized.error,
																"service_context_builder",
																"execute");
	}

	if (!dependencies_.execute_command) {
		return make_command_failure(contracts::ResultCode::RuntimeRetryExhausted,
																contracts::ResultCodeCategory::Runtime,
																"execute handler is not configured",
																"service_facade",
																"execute");
	}

	return dependencies_.execute_command(*normalized.context, request);
}

ExecutionCommandResult ServiceFacade::compensate(const ExecutionCompensationRequest& request) {
	if (dependencies_.context_builder == nullptr) {
		return make_command_failure(contracts::ResultCode::RuntimeRetryExhausted,
																contracts::ResultCodeCategory::Runtime,
																"service context builder is not configured",
																"service_facade",
																"compensate");
	}

	const auto normalized = dependencies_.context_builder->normalize_context(request.context);
	if (!normalized.ok()) {
		return make_command_failure(contracts::ResultCode::ValidationFieldMissing,
																contracts::ResultCodeCategory::Validation,
																normalized.error,
																"service_context_builder",
																"compensate");
	}

	if (!dependencies_.compensate_command) {
		return make_command_failure(contracts::ResultCode::RuntimeRetryExhausted,
																contracts::ResultCodeCategory::Runtime,
																"compensate handler is not configured",
																"service_facade",
																"compensate");
	}

	return dependencies_.compensate_command(*normalized.context, request);
}

ExecutionQueryResult ServiceFacade::query_state(const ExecutionQueryRequest& request) {
	if (dependencies_.context_builder == nullptr) {
		return make_query_failure(contracts::ResultCode::RuntimeRetryExhausted,
															contracts::ResultCodeCategory::Runtime,
															"service context builder is not configured",
															"service_facade",
															"query_state");
	}

	const auto normalized = dependencies_.context_builder->normalize_context(request.context);
	if (!normalized.ok()) {
		return make_query_failure(contracts::ResultCode::ValidationFieldMissing,
															contracts::ResultCodeCategory::Validation,
															normalized.error,
															"service_context_builder",
															"query_state");
	}

	if (!dependencies_.query_execution_state) {
		return make_query_failure(contracts::ResultCode::RuntimeRetryExhausted,
															contracts::ResultCodeCategory::Runtime,
															"query_state handler is not configured",
															"service_facade",
															"query_state");
	}

	return dependencies_.query_execution_state(*normalized.context, request);
}

ExecutionSubscriptionResult ServiceFacade::subscribe(const ExecutionSubscriptionRequest& request) {
	if (dependencies_.context_builder == nullptr) {
		return make_subscription_failure(contracts::ResultCode::RuntimeRetryExhausted,
																		 contracts::ResultCodeCategory::Runtime,
																		 "service context builder is not configured",
																		 "service_facade",
																		 "subscribe");
	}

	const auto normalized = dependencies_.context_builder->normalize_context(request.context);
	if (!normalized.ok()) {
		return make_subscription_failure(contracts::ResultCode::ValidationFieldMissing,
																		 contracts::ResultCodeCategory::Validation,
																		 normalized.error,
																		 "service_context_builder",
																		 "subscribe");
	}

	if (!dependencies_.subscribe_execution_state) {
		return make_subscription_failure(contracts::ResultCode::RuntimeRetryExhausted,
																		 contracts::ResultCodeCategory::Runtime,
																		 "subscribe handler is not configured",
																		 "service_facade",
																		 "subscribe");
	}

	return dependencies_.subscribe_execution_state(*normalized.context, request);
}

ExecutionDiagnoseResult ServiceFacade::diagnose(const ExecutionDiagnoseRequest& request) {
	if (dependencies_.context_builder == nullptr) {
		return make_diagnose_failure(contracts::ResultCode::RuntimeRetryExhausted,
																 contracts::ResultCodeCategory::Runtime,
																 "service context builder is not configured",
																 "service_facade",
																 "diagnose");
	}

	const auto normalized = dependencies_.context_builder->normalize_context(request.context);
	if (!normalized.ok()) {
		return make_diagnose_failure(contracts::ResultCode::ValidationFieldMissing,
																 contracts::ResultCodeCategory::Validation,
																 normalized.error,
																 "service_context_builder",
																 "diagnose");
	}

	if (!dependencies_.diagnose_execution_target) {
		return make_diagnose_failure(contracts::ResultCode::RuntimeRetryExhausted,
																 contracts::ResultCodeCategory::Runtime,
																 "diagnose handler is not configured",
																 "service_facade",
																 "diagnose");
	}

	return dependencies_.diagnose_execution_target(*normalized.context, request);
}

DataQueryResult ServiceFacade::query(const DataQueryRequest& request) {
	if (dependencies_.context_builder == nullptr) {
		return make_data_query_failure(contracts::ResultCode::RuntimeRetryExhausted,
																	 contracts::ResultCodeCategory::Runtime,
																	 "service context builder is not configured",
																	 "service_facade",
																	 "query");
	}

	const auto normalized = dependencies_.context_builder->normalize_context(request.context);
	if (!normalized.ok()) {
		return make_data_query_failure(contracts::ResultCode::ValidationFieldMissing,
																	 contracts::ResultCodeCategory::Validation,
																	 normalized.error,
																	 "service_context_builder",
																	 "query");
	}

	if (!dependencies_.query_data) {
		return make_data_query_failure(contracts::ResultCode::RuntimeRetryExhausted,
																	 contracts::ResultCodeCategory::Runtime,
																	 "query handler is not configured",
																	 "service_facade",
																	 "query");
	}

	return dependencies_.query_data(*normalized.context, request);
}

DataCatalogResult ServiceFacade::list_capabilities(const DataCatalogRequest& request) {
	if (dependencies_.context_builder == nullptr) {
		return make_data_catalog_failure(contracts::ResultCode::RuntimeRetryExhausted,
																		 contracts::ResultCodeCategory::Runtime,
																		 "service context builder is not configured",
																		 "service_facade",
																		 "list_capabilities");
	}

	const auto normalized = dependencies_.context_builder->normalize_context(request.context);
	if (!normalized.ok()) {
		return make_data_catalog_failure(contracts::ResultCode::ValidationFieldMissing,
																		 contracts::ResultCodeCategory::Validation,
																		 normalized.error,
																		 "service_context_builder",
																		 "list_capabilities");
	}

	if (!dependencies_.list_data_capabilities) {
		return make_data_catalog_failure(contracts::ResultCode::RuntimeRetryExhausted,
																		 contracts::ResultCodeCategory::Runtime,
																		 "list_capabilities handler is not configured",
																		 "service_facade",
																		 "list_capabilities");
	}

	return dependencies_.list_data_capabilities(*normalized.context, request);
}

}  // namespace dasall::services::internal