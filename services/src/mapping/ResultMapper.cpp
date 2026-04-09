#include "mapping/ResultMapper.h"

#include <utility>

namespace dasall::services::internal {

namespace {

using contracts::ErrorInfo;
using contracts::ResultCode;
using contracts::ResultCodeCategory;

[[nodiscard]] ServiceErrorClass classify_provider_status_code(
    const std::string_view& provider_status_code) {
  if (provider_status_code == "invalid_request") {
    return ServiceErrorClass::invalid_request;
  }

  if (provider_status_code == "capability_unsupported") {
    return ServiceErrorClass::capability_unsupported;
  }

  if (provider_status_code == "policy_denied" ||
      provider_status_code == "confirmation_required" ||
      provider_status_code == "route_not_permitted") {
    return ServiceErrorClass::policy_denied;
  }

  if (provider_status_code == "route_unavailable" ||
      provider_status_code == "fallback_blocked" ||
      provider_status_code == "adapter_not_registered" ||
      provider_status_code == "route_kind_mismatch") {
    return ServiceErrorClass::route_unavailable;
  }

  if (provider_status_code == "adapter_unavailable" ||
      provider_status_code == "platform_hal_unbound" ||
      provider_status_code == "platform_hal_exception" ||
      provider_status_code == "local_service_unbound" ||
      provider_status_code == "remote_service_stub" ||
      provider_status_code == "bridge_exception") {
    return ServiceErrorClass::adapter_unavailable;
  }

  if (provider_status_code == "target_busy") {
    return ServiceErrorClass::target_busy;
  }

  if (provider_status_code == "partial_side_effect") {
    return ServiceErrorClass::partial_side_effect;
  }

  if (provider_status_code == "data_stale") {
    return ServiceErrorClass::data_stale;
  }

  if (provider_status_code == "subscription_overflow") {
    return ServiceErrorClass::subscription_overflow;
  }

  return ServiceErrorClass::none;
}

[[nodiscard]] ServiceErrorClass classify_receipt(const AdapterReceipt& receipt) {
  const auto explicit_class = classify_provider_status_code(receipt.provider_status_code);
  if (explicit_class != ServiceErrorClass::none) {
    return explicit_class;
  }

  switch (receipt.transport_outcome) {
    case AdapterTransportOutcome::acknowledged:
      return ServiceErrorClass::none;
    case AdapterTransportOutcome::timeout:
    case AdapterTransportOutcome::unreachable:
      return ServiceErrorClass::adapter_unavailable;
    case AdapterTransportOutcome::rejected:
      return ServiceErrorClass::invalid_request;
    case AdapterTransportOutcome::partial:
      return ServiceErrorClass::partial_side_effect;
  }

  return ServiceErrorClass::invalid_request;
}

[[nodiscard]] ResultCodeCategory failure_type_for(ServiceErrorClass error_class) {
  switch (error_class) {
    case ServiceErrorClass::invalid_request:
    case ServiceErrorClass::capability_unsupported:
      return ResultCodeCategory::Validation;
    case ServiceErrorClass::policy_denied:
      return ResultCodeCategory::Policy;
    case ServiceErrorClass::route_unavailable:
    case ServiceErrorClass::data_stale:
    case ServiceErrorClass::subscription_overflow:
      return ResultCodeCategory::Runtime;
    case ServiceErrorClass::adapter_unavailable:
    case ServiceErrorClass::target_busy:
    case ServiceErrorClass::partial_side_effect:
      return ResultCodeCategory::Provider;
    case ServiceErrorClass::none:
      return ResultCodeCategory::Unknown;
  }

  return ResultCodeCategory::Unknown;
}

[[nodiscard]] ResultCode result_code_for(ServiceErrorClass error_class) {
  switch (error_class) {
    case ServiceErrorClass::invalid_request:
    case ServiceErrorClass::capability_unsupported:
      return ResultCode::ValidationFieldMissing;
    case ServiceErrorClass::policy_denied:
      return ResultCode::PolicyDenied;
    case ServiceErrorClass::route_unavailable:
    case ServiceErrorClass::data_stale:
    case ServiceErrorClass::subscription_overflow:
      return ResultCode::RuntimeRetryExhausted;
    case ServiceErrorClass::adapter_unavailable:
    case ServiceErrorClass::target_busy:
    case ServiceErrorClass::partial_side_effect:
      return ResultCode::ProviderTimeout;
    case ServiceErrorClass::none:
      return ResultCode::ToolExecutionFailed;
  }

  return ResultCode::ToolExecutionFailed;
}

[[nodiscard]] bool retryable_for(ServiceErrorClass error_class) {
  switch (error_class) {
    case ServiceErrorClass::invalid_request:
    case ServiceErrorClass::capability_unsupported:
    case ServiceErrorClass::policy_denied:
    case ServiceErrorClass::partial_side_effect:
    case ServiceErrorClass::none:
      return false;
    case ServiceErrorClass::route_unavailable:
    case ServiceErrorClass::adapter_unavailable:
    case ServiceErrorClass::target_busy:
    case ServiceErrorClass::data_stale:
    case ServiceErrorClass::subscription_overflow:
      return true;
  }

  return false;
}

[[nodiscard]] bool safe_to_replan_for(ServiceErrorClass error_class) {
  switch (error_class) {
    case ServiceErrorClass::invalid_request:
    case ServiceErrorClass::capability_unsupported:
    case ServiceErrorClass::policy_denied:
    case ServiceErrorClass::route_unavailable:
    case ServiceErrorClass::adapter_unavailable:
    case ServiceErrorClass::data_stale:
      return true;
    case ServiceErrorClass::target_busy:
    case ServiceErrorClass::partial_side_effect:
    case ServiceErrorClass::subscription_overflow:
    case ServiceErrorClass::none:
      return false;
  }

  return false;
}

[[nodiscard]] std::string source_ref_type_for(ServiceErrorClass error_class) {
  switch (error_class) {
    case ServiceErrorClass::invalid_request:
      return "request_validator";
    case ServiceErrorClass::capability_unsupported:
      return "capability_snapshot";
    case ServiceErrorClass::policy_denied:
      return "policy_decision";
    case ServiceErrorClass::route_unavailable:
      return "route_receipt";
    case ServiceErrorClass::adapter_unavailable:
      return "adapter_receipt";
    case ServiceErrorClass::target_busy:
      return "target_lease";
    case ServiceErrorClass::partial_side_effect:
      return "evidence_ref";
    case ServiceErrorClass::data_stale:
      return "snapshot_ref";
    case ServiceErrorClass::subscription_overflow:
      return "subscription_stream";
    case ServiceErrorClass::none:
      return "adapter_receipt";
  }

  return "adapter_receipt";
}

[[nodiscard]] std::string source_ref_id_for(ServiceErrorClass error_class,
                                            const AdapterReceipt& receipt) {
  if (error_class == ServiceErrorClass::partial_side_effect &&
      !receipt.evidence_refs.empty()) {
    return receipt.evidence_refs.front();
  }

  if ((error_class == ServiceErrorClass::capability_unsupported ||
       error_class == ServiceErrorClass::policy_denied ||
       error_class == ServiceErrorClass::data_stale ||
       error_class == ServiceErrorClass::subscription_overflow) &&
      !receipt.evidence_refs.empty()) {
    return receipt.evidence_refs.front();
  }

  if (!receipt.receipt_ref.empty()) {
    return receipt.receipt_ref;
  }

  if (!receipt.adapter_id.empty()) {
    return receipt.adapter_id;
  }

  return std::string(service_error_class_name(error_class));
}

[[nodiscard]] std::string build_error_message(ServiceErrorClass error_class,
                                              const AdapterReceipt& receipt) {
  std::string message(service_error_class_name(error_class));
  if (!receipt.provider_status_code.empty()) {
    message += ": ";
    message += receipt.provider_status_code;
  }

  if (!receipt.adapter_id.empty()) {
    message += " adapter=";
    message += receipt.adapter_id;
  }

  if (!receipt.target_id.empty()) {
    message += " target=";
    message += receipt.target_id;
  }

  return message;
}

[[nodiscard]] ErrorInfo make_error_info(ServiceErrorClass error_class,
                                        const AdapterReceipt& receipt,
                                        std::string message) {
  const auto result_code = result_code_for(error_class);
  return ErrorInfo{
      .failure_type = failure_type_for(error_class),
      .retryable = retryable_for(error_class),
      .safe_to_replan = safe_to_replan_for(error_class),
      .details = {
          .code = static_cast<int>(result_code),
          .message = std::move(message),
          .stage = "result_mapper",
      },
      .source_ref = {
          .ref_type = source_ref_type_for(error_class),
          .ref_id = source_ref_id_for(error_class, receipt),
      },
  };
}

[[nodiscard]] ResultMapping make_success_mapping(const AdapterReceipt& receipt) {
  return ResultMapping{
      .error_class = ServiceErrorClass::none,
      .success = true,
      .code = ResultCode::ToolExecutionFailed,
      .error = std::nullopt,
      .side_effects = receipt.side_effects,
      .compensation_hints = {},
      .evidence_refs = receipt.evidence_refs,
      .payload_json = receipt.payload_json,
      .resync_required = false,
      .dropped_count = 0U,
  };
}

[[nodiscard]] ResultMapping make_invalid_partial_contract_mapping(
    const AdapterReceipt& receipt) {
  constexpr std::string_view kInvalidPartialMessage =
      "partial_side_effect requires side_effects, evidence_refs, and compensation_hints";
  return ResultMapping{
      .error_class = ServiceErrorClass::invalid_request,
      .success = false,
      .code = ResultCode::ValidationFieldMissing,
      .error = make_error_info(ServiceErrorClass::invalid_request,
                               receipt,
                               std::string(kInvalidPartialMessage)),
      .side_effects = {},
      .compensation_hints = {},
      .evidence_refs = {},
      .payload_json = receipt.payload_json,
      .resync_required = false,
      .dropped_count = 0U,
  };
}

[[nodiscard]] ResultMapping make_failure_mapping(
    ServiceErrorClass error_class,
    const AdapterReceipt& receipt,
    const std::vector<std::string>& compensation_hints) {
  const auto partial_side_effect = error_class == ServiceErrorClass::partial_side_effect;
  return ResultMapping{
      .error_class = error_class,
      .success = false,
      .code = result_code_for(error_class),
      .error = make_error_info(error_class, receipt, build_error_message(error_class, receipt)),
      .side_effects = partial_side_effect ? receipt.side_effects : std::vector<std::string>{},
      .compensation_hints =
          partial_side_effect ? compensation_hints : std::vector<std::string>{},
      .evidence_refs = partial_side_effect ? receipt.evidence_refs : std::vector<std::string>{},
      .payload_json = receipt.payload_json,
      .resync_required = error_class == ServiceErrorClass::subscription_overflow,
      .dropped_count = 0U,
  };
}

}  // namespace

std::string_view service_error_class_name(ServiceErrorClass error_class) {
  switch (error_class) {
    case ServiceErrorClass::none:
      return "none";
    case ServiceErrorClass::invalid_request:
      return "invalid_request";
    case ServiceErrorClass::capability_unsupported:
      return "capability_unsupported";
    case ServiceErrorClass::policy_denied:
      return "policy_denied";
    case ServiceErrorClass::route_unavailable:
      return "route_unavailable";
    case ServiceErrorClass::adapter_unavailable:
      return "adapter_unavailable";
    case ServiceErrorClass::target_busy:
      return "target_busy";
    case ServiceErrorClass::partial_side_effect:
      return "partial_side_effect";
    case ServiceErrorClass::data_stale:
      return "data_stale";
    case ServiceErrorClass::subscription_overflow:
      return "subscription_overflow";
  }

  return "unknown";
}

ResultMapping ResultMapper::map_result(
    const AdapterReceipt& receipt,
    const std::vector<std::string>& compensation_hints) const {
  const auto error_class = classify_receipt(receipt);
  if (error_class == ServiceErrorClass::none) {
    return make_success_mapping(receipt);
  }

  if (error_class == ServiceErrorClass::partial_side_effect &&
      (receipt.side_effects.empty() || receipt.evidence_refs.empty() ||
       compensation_hints.empty())) {
    return make_invalid_partial_contract_mapping(receipt);
  }

  return make_failure_mapping(error_class, receipt, compensation_hints);
}

ExecutionCommandResult ResultMapper::to_execution_command_result(
    const AdapterReceipt& receipt,
    const std::vector<std::string>& compensation_hints,
    std::string execution_id,
    contracts::ResultCode success_code) const {
  const auto mapped = map_result(receipt, compensation_hints);
  return ExecutionCommandResult{
      .code = mapped.success ? success_code : mapped.code,
      .execution_id = std::move(execution_id),
      .payload_json = mapped.payload_json,
      .side_effects = mapped.side_effects,
      .compensation_hints = mapped.compensation_hints,
      .error = mapped.error,
  };
}

ExecutionQueryResult ResultMapper::to_execution_query_result(
    const AdapterReceipt& receipt,
    std::string state,
    bool from_cache,
    contracts::ResultCode success_code) const {
  const auto mapped = map_result(receipt, {});
  return ExecutionQueryResult{
      .code = mapped.success ? success_code : mapped.code,
      .state = mapped.success ? std::move(state) : std::string{},
      .snapshot_json = mapped.success ? mapped.payload_json : std::string{},
      .from_cache = from_cache,
      .error = mapped.error,
  };
}

ExecutionSubscriptionResult ResultMapper::to_execution_subscription_result(
    const AdapterReceipt& receipt,
    std::optional<std::string> next_cursor,
    std::uint32_t dropped_count,
    contracts::ResultCode success_code) const {
  const auto mapped = map_result(receipt, {});
  const auto subscription_dropped_count =
      mapped.resync_required ? dropped_count : mapped.dropped_count;
  return ExecutionSubscriptionResult{
      .code = mapped.success ? success_code : mapped.code,
      .events_json = mapped.success ? mapped.payload_json : std::string{},
      .next_cursor = mapped.success ? std::move(next_cursor) : std::nullopt,
      .resync_required = mapped.resync_required,
      .dropped_count = subscription_dropped_count,
      .error = mapped.error,
  };
}

ExecutionDiagnoseResult ResultMapper::to_execution_diagnose_result(
    const AdapterReceipt& receipt,
    bool target_reachable_on_success,
    contracts::ResultCode success_code) const {
  const auto mapped = map_result(receipt, {});
  return ExecutionDiagnoseResult{
      .code = mapped.success ? success_code : mapped.code,
      .target_reachable = mapped.success ? target_reachable_on_success : false,
      .report_json = mapped.success ? mapped.payload_json : std::string{},
      .error = mapped.error,
  };
}

DataQueryResult ResultMapper::to_data_query_result(
    const AdapterReceipt& receipt,
    bool from_cache,
    contracts::ResultCode success_code) const {
  const auto mapped = map_result(receipt, {});
  return DataQueryResult{
      .code = mapped.success ? success_code : mapped.code,
      .rows_json = mapped.success ? mapped.payload_json : std::string{},
      .from_cache = from_cache,
      .error = mapped.error,
  };
}

DataCatalogResult ResultMapper::to_data_catalog_result(
    const AdapterReceipt& receipt,
    contracts::ResultCode success_code) const {
  const auto mapped = map_result(receipt, {});
  return DataCatalogResult{
      .code = mapped.success ? success_code : mapped.code,
      .catalog_json = mapped.success ? mapped.payload_json : std::string{},
      .error = mapped.error,
  };
}

}  // namespace dasall::services::internal