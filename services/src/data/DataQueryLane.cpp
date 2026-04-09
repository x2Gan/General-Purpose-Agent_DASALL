#include "data/DataQueryLane.h"

#include <string_view>
#include <utility>

namespace dasall::services::internal {

namespace {

constexpr std::string_view kCatalogOperation = "catalog.list";

[[nodiscard]] std::string make_error_payload(std::string_view error_code,
                                             std::string_view message) {
  return std::string("{\"error\":\"") + std::string(error_code) +
         "\",\"message\":\"" + std::string(message) + "\"}";
}

[[nodiscard]] std::string normalize_filters_json(const std::string& filters_json) {
  return filters_json.empty() ? "{}" : filters_json;
}

[[nodiscard]] std::string make_query_payload(const DataQueryRequest& request) {
  return std::string("{\"dataset\":\"") + request.dataset +
         "\",\"filters\":" + normalize_filters_json(request.filters_json) +
         ",\"projection\":\"" + request.projection + "\"}";
}

[[nodiscard]] std::string make_catalog_payload(const DataCatalogRequest& request) {
  return std::string("{\"target_class\":\"") + request.target_class + "\"}";
}

[[nodiscard]] AdapterReceipt make_receipt(std::string receipt_ref,
                                          std::string target_id,
                                          std::string provider_status_code,
                                          std::string payload_json,
                                          std::vector<std::string> evidence_refs) {
  return AdapterReceipt{
      .receipt_ref = std::move(receipt_ref),
      .adapter_id = {},
      .route_kind = AdapterRouteKind::local_service,
      .target_id = std::move(target_id),
      .transport_outcome = AdapterTransportOutcome::rejected,
      .provider_status_code = std::move(provider_status_code),
      .payload_json = std::move(payload_json),
      .latency_ms = 0U,
      .side_effects = {},
      .evidence_refs = std::move(evidence_refs),
  };
}

void apply_error_override(std::optional<contracts::ErrorInfo>* error,
                          std::string message,
                          std::string stage) {
  if (!error->has_value()) {
    return;
  }

  (*error)->details.message = std::move(message);
  (*error)->details.stage = std::move(stage);
}

[[nodiscard]] AdapterRouteRequest build_query_route_request(const ServicePolicyView& policy_view,
                                                            const CapabilitySnapshotView& snapshot,
                                                            const FallbackEnvelope& fallback_envelope,
                                                            const std::vector<AdapterCandidateView>& candidates,
                                                            const DataQueryRequest& request) {
  return AdapterRouteRequest{
      .capability_id = request.dataset,
      .target_id = request.dataset,
      .request_kind = AdapterRouteRequestKind::query,
      .requested_operation = request.projection,
      .high_risk = false,
      .minimum_trust = AdapterTrustClass::untrusted,
      .policy_view = policy_view,
      .capability_snapshot = snapshot,
      .fallback_envelope = fallback_envelope,
      .registered_candidates = candidates,
  };
}

[[nodiscard]] AdapterRouteRequest build_catalog_route_request(const ServicePolicyView& policy_view,
                                                              const CapabilitySnapshotView& snapshot,
                                                              const FallbackEnvelope& fallback_envelope,
                                                              const std::vector<AdapterCandidateView>& candidates,
                                                              const DataCatalogRequest& request) {
  return AdapterRouteRequest{
      .capability_id = request.target_class,
      .target_id = request.target_class,
      .request_kind = AdapterRouteRequestKind::query,
      .requested_operation = std::string(kCatalogOperation),
      .high_risk = false,
      .minimum_trust = AdapterTrustClass::untrusted,
      .policy_view = policy_view,
      .capability_snapshot = snapshot,
      .fallback_envelope = fallback_envelope,
      .registered_candidates = candidates,
  };
}

}  // namespace

DataQueryLane::DataQueryLane(DataQueryLaneDependencies dependencies)
    : dependencies_(std::move(dependencies)) {}

DataQueryResult DataQueryLane::query(const ServiceCallContext& context,
                                     const DataQueryRequest& request) {
  if (request.dataset.empty() || request.projection.empty()) {
    return make_query_error_result(request.dataset,
                                   "validator:" + context.request_id,
                                   "invalid_request",
                                   "dataset and projection are required",
                                   "data_query_lane",
                                   {},
                                   false);
  }

  if (dependencies_.router == nullptr || dependencies_.bridge == nullptr ||
      dependencies_.result_mapper == nullptr || dependencies_.projection_cache == nullptr) {
    return make_runtime_query_failure("data query lane dependencies are not configured",
                                      "data_query_lane",
                                      "data_query_lane.dependencies");
  }

  const auto cached = dependencies_.projection_cache->lookup(request, request.freshness);
  if (cached.snapshot.has_value() && cached.from_cache) {
    return DataQueryResult{
        .code = contracts::ResultCode::ToolExecutionFailed,
        .rows_json = cached.snapshot->rows_json,
        .from_cache = true,
        .error = std::nullopt,
    };
  }

  if (cached.state == ProjectionCacheState::hit_stale && cached.snapshot.has_value()) {
    return make_query_error_result(request.dataset,
                                   cached.snapshot->cache_ref,
                                   "data_stale",
                                   "cached projection is stale under strict freshness",
                                   "data_query_lane",
                                   {cached.snapshot->cache_ref},
                                   false);
  }

  const auto route_decision = dependencies_.router->select_adapter(build_query_route_request(
      dependencies_.policy_view,
      dependencies_.capability_snapshot,
      dependencies_.fallback_envelope,
      dependencies_.registered_candidates,
      request));
  if (!route_decision.ok()) {
    return make_query_error_result(request.dataset,
                                   "route:" + context.request_id,
                                   std::string(route_failure_name(route_decision.failure)),
                                   route_decision.reason,
                                   "adapter_router",
                                   route_decision.failure == AdapterRouteFailure::route_not_permitted
                                       ? std::vector<std::string>{"policy://route/" + context.request_id}
                                       : std::vector<std::string>{},
                                   false);
  }

  const auto receipt = dependencies_.bridge->invoke(
      *route_decision.selection,
      AdapterInvocationRequest{
          .request_id = context.request_id,
          .capability_id = request.dataset,
          .target_id = request.dataset,
          .request_kind = AdapterRouteRequestKind::query,
          .operation_name = request.projection,
          .payload_json = make_query_payload(request),
      });

  if (!receipt.side_effects.empty()) {
    return make_query_error_result(request.dataset,
                                   receipt.receipt_ref.empty() ? "data_query_receipt:" + context.request_id
                                                               : receipt.receipt_ref,
                                   "invalid_request",
                                   "data query must not emit side_effects",
                                   "data_query_lane",
                                   receipt.evidence_refs,
                                   false);
  }

  auto result = dependencies_.result_mapper->to_data_query_result(receipt, false);
  if (!result.error.has_value()) {
    dependencies_.projection_cache->store(request,
                                          receipt.payload_json,
                                          !receipt.evidence_refs.empty() ? receipt.evidence_refs.front()
                                                                         : std::string{});
  } else if (!receipt.provider_status_code.empty()) {
    result.error->details.stage = "data_query_lane";
  }

  return result;
}

DataCatalogResult DataQueryLane::list_capabilities(const ServiceCallContext& context,
                                                   const DataCatalogRequest& request) const {
  if (request.target_class.empty()) {
    return make_catalog_error_result(request.target_class,
                                     "validator:" + context.request_id,
                                     "invalid_request",
                                     "target_class is required",
                                     "data_query_lane",
                                     {});
  }

  if (dependencies_.router == nullptr || dependencies_.bridge == nullptr ||
      dependencies_.result_mapper == nullptr) {
    return make_runtime_catalog_failure("data query lane dependencies are not configured",
                                        "data_query_lane",
                                        "data_query_lane.dependencies");
  }

  const auto route_decision = dependencies_.router->select_adapter(build_catalog_route_request(
      dependencies_.policy_view,
      dependencies_.capability_snapshot,
      dependencies_.fallback_envelope,
      dependencies_.registered_candidates,
      request));
  if (!route_decision.ok()) {
    return make_catalog_error_result(request.target_class,
                                     "route:" + context.request_id,
                                     std::string(route_failure_name(route_decision.failure)),
                                     route_decision.reason,
                                     "adapter_router",
                                     route_decision.failure == AdapterRouteFailure::route_not_permitted
                                         ? std::vector<std::string>{"policy://route/" + context.request_id}
                                         : std::vector<std::string>{});
  }

  const auto receipt = dependencies_.bridge->invoke(
      *route_decision.selection,
      AdapterInvocationRequest{
          .request_id = context.request_id,
          .capability_id = request.target_class,
          .target_id = request.target_class,
          .request_kind = AdapterRouteRequestKind::query,
          .operation_name = std::string(kCatalogOperation),
          .payload_json = make_catalog_payload(request),
      });

  if (!receipt.side_effects.empty()) {
    return make_catalog_error_result(
        request.target_class,
        receipt.receipt_ref.empty() ? "data_catalog_receipt:" + context.request_id
                                    : receipt.receipt_ref,
        "invalid_request",
        "data capability listing must not emit side_effects",
        "data_query_lane",
        receipt.evidence_refs);
  }

  auto result = dependencies_.result_mapper->to_data_catalog_result(receipt);
  if (result.error.has_value() && !receipt.provider_status_code.empty()) {
    result.error->details.stage = "data_query_lane";
  }

  return result;
}

DataQueryResult DataQueryLane::make_runtime_query_failure(const std::string& message,
                                                          const std::string& stage,
                                                          const std::string& ref_id) const {
  return DataQueryResult{
      .code = contracts::ResultCode::RuntimeRetryExhausted,
      .rows_json = {},
      .from_cache = false,
      .error = contracts::ErrorInfo{
          .failure_type = contracts::ResultCodeCategory::Runtime,
          .retryable = false,
          .safe_to_replan = false,
          .details = {
              .code = static_cast<int>(contracts::ResultCode::RuntimeRetryExhausted),
              .message = message,
              .stage = stage,
          },
          .source_ref = {
              .ref_type = "services",
              .ref_id = ref_id,
          },
      },
  };
}

DataCatalogResult DataQueryLane::make_runtime_catalog_failure(const std::string& message,
                                                              const std::string& stage,
                                                              const std::string& ref_id) const {
  return DataCatalogResult{
      .code = contracts::ResultCode::RuntimeRetryExhausted,
      .catalog_json = {},
      .error = contracts::ErrorInfo{
          .failure_type = contracts::ResultCodeCategory::Runtime,
          .retryable = false,
          .safe_to_replan = false,
          .details = {
              .code = static_cast<int>(contracts::ResultCode::RuntimeRetryExhausted),
              .message = message,
              .stage = stage,
          },
          .source_ref = {
              .ref_type = "services",
              .ref_id = ref_id,
          },
      },
  };
}

DataQueryResult DataQueryLane::make_query_error_result(const std::string& dataset,
                                                       const std::string& receipt_ref,
                                                       const std::string& provider_status_code,
                                                       const std::string& message,
                                                       const std::string& stage,
                                                       std::vector<std::string> evidence_refs,
                                                       bool from_cache) const {
  if (dependencies_.result_mapper == nullptr) {
    return make_runtime_query_failure(message, stage, receipt_ref);
  }

  auto result = dependencies_.result_mapper->to_data_query_result(
      make_receipt(receipt_ref,
                   dataset,
                   provider_status_code,
                   make_error_payload(provider_status_code, message),
                   std::move(evidence_refs)),
      from_cache);
  apply_error_override(&result.error, message, stage);
  return result;
}

DataCatalogResult DataQueryLane::make_catalog_error_result(
    const std::string& target_class,
    const std::string& receipt_ref,
    const std::string& provider_status_code,
    const std::string& message,
    const std::string& stage,
    std::vector<std::string> evidence_refs) const {
  if (dependencies_.result_mapper == nullptr) {
    return make_runtime_catalog_failure(message, stage, receipt_ref);
  }

  auto result = dependencies_.result_mapper->to_data_catalog_result(
      make_receipt(receipt_ref,
                   target_class,
                   provider_status_code,
                   make_error_payload(provider_status_code, message),
                   std::move(evidence_refs)));
  apply_error_override(&result.error, message, stage);
  return result;
}

}  // namespace dasall::services::internal