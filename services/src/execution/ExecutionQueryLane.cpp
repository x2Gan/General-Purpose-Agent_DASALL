#include "execution/ExecutionQueryLane.h"

#include <utility>

#include "bridges/ServiceMetricsBridge.h"

namespace dasall::services::internal {

namespace {

[[nodiscard]] std::string make_error_payload(std::string_view error_code,
                                             std::string_view message) {
  return std::string("{\"error\":\"") + std::string(error_code) +
         "\",\"message\":\"" + std::string(message) + "\"}";
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

[[nodiscard]] std::string freshness_name(ServiceDataFreshness freshness) {
  return freshness == ServiceDataFreshness::allow_stale ? "allow_stale" : "strict";
}

[[nodiscard]] AdapterRouteRequest build_route_request(const ServicePolicyView& policy_view,
                                                      const CapabilitySnapshotView& snapshot,
                                                      const FallbackEnvelope& fallback_envelope,
                                                      const std::vector<AdapterCandidateView>& candidates,
                                                      const ExecutionQueryRequest& request) {
  return AdapterRouteRequest{
      .capability_id = request.target.capability_id,
      .target_id = request.target.target_id,
      .request_kind = AdapterRouteRequestKind::query,
      .requested_operation = request.query_kind,
      .high_risk = false,
      .minimum_trust = AdapterTrustClass::untrusted,
      .policy_view = policy_view,
      .capability_snapshot = snapshot,
      .fallback_envelope = fallback_envelope,
      .registered_candidates = candidates,
  };
}

}  // namespace

ExecutionQueryLane::ExecutionQueryLane(ExecutionQueryLaneDependencies dependencies)
    : dependencies_(std::move(dependencies)) {}

ExecutionQueryResult ExecutionQueryLane::query_state(const ServiceCallContext& context,
                                                     const ExecutionQueryRequest& request) const {
  const auto emit_metrics = [&](ExecutionQueryResult result,
                                std::string_view adapter_id,
                                std::optional<std::uint64_t> latency_ms)
      -> ExecutionQueryResult {
    if (dependencies_.metrics_bridge != nullptr) {
      (void)dependencies_.metrics_bridge->record_execution_query_result(
          request.query_kind,
          adapter_id,
          result,
          latency_ms);
    }

    return result;
  };

  if (request.target.capability_id.empty() || request.target.target_id.empty() ||
      request.query_kind.empty()) {
    return emit_metrics(make_error_result(request.target.target_id,
                                          "validator:" + context.request_id,
                                          "invalid_request",
                                          "capability_id, target_id, and query_kind are required",
                                          "execution_query_lane",
                                          {},
                                          false),
                        {},
                        std::nullopt);
  }

  if (dependencies_.router == nullptr || dependencies_.bridge == nullptr ||
      dependencies_.result_mapper == nullptr) {
    return emit_metrics(make_runtime_failure("query lane dependencies are not configured",
                                             "execution_query_lane",
                                             "query_lane.dependencies"),
                        {},
                        std::nullopt);
  }

  const auto route_decision = dependencies_.router->select_adapter(build_route_request(
      dependencies_.policy_view,
      dependencies_.capability_snapshot,
      dependencies_.fallback_envelope,
      dependencies_.registered_candidates,
      request));
  if (!route_decision.ok()) {
    return emit_metrics(make_error_result(request.target.target_id,
                        "route:" + context.request_id,
                        std::string(route_failure_name(route_decision.failure)),
                        route_decision.reason,
                        "adapter_router",
                        route_decision.failure == AdapterRouteFailure::route_not_permitted
                          ? std::vector<std::string>{"policy://route/" + context.request_id}
                          : std::vector<std::string>{},
                        false),
              {},
              std::nullopt);
  }

  const auto receipt = dependencies_.bridge->invoke(
      *route_decision.selection,
      AdapterInvocationRequest{
          .request_id = context.request_id,
          .capability_id = request.target.capability_id,
          .target_id = request.target.target_id,
          .request_kind = AdapterRouteRequestKind::query,
          .operation_name = request.query_kind,
          .payload_json = std::string("{\"freshness\":\"") + freshness_name(request.freshness) +
                          "\"}",
      });

  if (!receipt.side_effects.empty()) {
    return emit_metrics(make_error_result(request.target.target_id,
                                          receipt.receipt_ref.empty() ? "query_receipt:" + context.request_id
                                                                      : receipt.receipt_ref,
                                          "invalid_request",
                                          "query_state must not emit side_effects",
                                          "execution_query_lane",
                                          receipt.evidence_refs,
                                          false),
                        route_decision.selection->adapter_id,
                        receipt.latency_ms);
  }

  if (request.freshness == ServiceDataFreshness::allow_stale &&
      receipt.provider_status_code == "data_stale" && dependencies_.load_cached_snapshot) {
    const auto cached_snapshot = dependencies_.load_cached_snapshot(request);
    if (cached_snapshot.has_value()) {
      return emit_metrics(ExecutionQueryResult{
          .code = contracts::ResultCode::ToolExecutionFailed,
          .state = cached_snapshot->state,
          .snapshot_json = cached_snapshot->snapshot_json,
          .from_cache = true,
          .error = std::nullopt,
      },
      route_decision.selection->adapter_id,
      std::nullopt);
    }
  }

  auto result = dependencies_.result_mapper->to_execution_query_result(
      receipt,
      dependencies_.extract_state ? dependencies_.extract_state(receipt, request)
                                  : request.query_kind,
      false);
  if (result.error.has_value() && !receipt.provider_status_code.empty()) {
    result.error->details.stage = "execution_query_lane";
  }

  return emit_metrics(std::move(result), route_decision.selection->adapter_id, receipt.latency_ms);
}

ExecutionQueryResult ExecutionQueryLane::make_runtime_failure(const std::string& message,
                                                              const std::string& stage,
                                                              const std::string& ref_id) const {
  return ExecutionQueryResult{
      .code = contracts::ResultCode::RuntimeRetryExhausted,
      .state = {},
      .snapshot_json = {},
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

ExecutionQueryResult ExecutionQueryLane::make_error_result(const std::string& target_id,
                                                           const std::string& receipt_ref,
                                                           const std::string& provider_status_code,
                                                           const std::string& message,
                                                           const std::string& stage,
                                                           std::vector<std::string> evidence_refs,
                                                           bool from_cache) const {
  if (dependencies_.result_mapper == nullptr) {
    return make_runtime_failure(message, stage, receipt_ref);
  }

  auto result = dependencies_.result_mapper->to_execution_query_result(
      make_receipt(receipt_ref,
                   target_id,
                   provider_status_code,
                   make_error_payload(provider_status_code, message),
                   std::move(evidence_refs)),
      {},
      from_cache);
  apply_error_override(&result.error, message, stage);
  return result;
}

}  // namespace dasall::services::internal