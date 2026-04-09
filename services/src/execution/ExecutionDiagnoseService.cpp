#include "execution/ExecutionDiagnoseService.h"

#include <string_view>
#include <utility>

namespace dasall::services::internal {

namespace {

constexpr std::string_view kDiagnoseOperation = "diagnose";

[[nodiscard]] std::string make_error_payload(std::string_view error_code,
                                             std::string_view message) {
  return std::string("{\"error\":\"") + std::string(error_code) +
         "\",\"message\":\"" + std::string(message) + "\"}";
}

[[nodiscard]] std::string make_request_payload(bool include_last_error) {
  return include_last_error ? "{\"include_last_error\":true}"
                            : "{\"include_last_error\":false}";
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

[[nodiscard]] AdapterRouteRequest build_route_request(const ServicePolicyView& policy_view,
                                                      const CapabilitySnapshotView& snapshot,
                                                      const FallbackEnvelope& fallback_envelope,
                                                      const std::vector<AdapterCandidateView>& candidates,
                                                      const ExecutionDiagnoseRequest& request) {
  return AdapterRouteRequest{
      .capability_id = request.target.capability_id,
      .target_id = request.target.target_id,
      .request_kind = AdapterRouteRequestKind::query,
      .requested_operation = std::string(kDiagnoseOperation),
      .high_risk = false,
      .minimum_trust = AdapterTrustClass::untrusted,
      .policy_view = policy_view,
      .capability_snapshot = snapshot,
      .fallback_envelope = fallback_envelope,
      .registered_candidates = candidates,
  };
}

}  // namespace

ExecutionDiagnoseService::ExecutionDiagnoseService(ExecutionDiagnoseServiceDependencies dependencies)
    : dependencies_(std::move(dependencies)) {}

ExecutionDiagnoseResult ExecutionDiagnoseService::diagnose(
    const ServiceCallContext& context,
    const ExecutionDiagnoseRequest& request) const {
  if (request.target.capability_id.empty() || request.target.target_id.empty()) {
    return make_error_result(request.target.target_id,
                             "validator:" + context.request_id,
                             "invalid_request",
                             "capability_id and target_id are required",
                             "execution_diagnose_service",
                             {});
  }

  if (dependencies_.router == nullptr || dependencies_.bridge == nullptr ||
      dependencies_.result_mapper == nullptr) {
    return make_runtime_failure("diagnose service dependencies are not configured",
                                "execution_diagnose_service",
                                "execution_diagnose_service.dependencies");
  }

  const auto route_decision = dependencies_.router->select_adapter(build_route_request(
      dependencies_.policy_view,
      dependencies_.capability_snapshot,
      dependencies_.fallback_envelope,
      dependencies_.registered_candidates,
      request));
  if (!route_decision.ok()) {
    return make_error_result(request.target.target_id,
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
          .capability_id = request.target.capability_id,
          .target_id = request.target.target_id,
          .request_kind = AdapterRouteRequestKind::query,
          .operation_name = std::string(kDiagnoseOperation),
          .payload_json = make_request_payload(request.include_last_error),
      });

  if (!receipt.side_effects.empty()) {
    return make_error_result(request.target.target_id,
                             receipt.receipt_ref.empty() ? "diagnose_receipt:" + context.request_id
                                                         : receipt.receipt_ref,
                             "invalid_request",
                             "diagnose must not emit side_effects",
                             "execution_diagnose_service",
                             receipt.evidence_refs);
  }

  auto result = dependencies_.result_mapper->to_execution_diagnose_result(receipt, true);
  if (result.error.has_value() && !receipt.provider_status_code.empty()) {
    result.error->details.stage = "execution_diagnose_service";
  }

  return result;
}

ExecutionDiagnoseResult ExecutionDiagnoseService::make_runtime_failure(
    const std::string& message,
    const std::string& stage,
    const std::string& ref_id) const {
  return ExecutionDiagnoseResult{
      .code = contracts::ResultCode::RuntimeRetryExhausted,
      .target_reachable = false,
      .report_json = {},
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

ExecutionDiagnoseResult ExecutionDiagnoseService::make_error_result(
    const std::string& target_id,
    const std::string& receipt_ref,
    const std::string& provider_status_code,
    const std::string& message,
    const std::string& stage,
    std::vector<std::string> evidence_refs) const {
  if (dependencies_.result_mapper == nullptr) {
    return make_runtime_failure(message, stage, receipt_ref);
  }

  auto result = dependencies_.result_mapper->to_execution_diagnose_result(
      make_receipt(receipt_ref,
                   target_id,
                   provider_status_code,
                   make_error_payload(provider_status_code, message),
                   std::move(evidence_refs)),
      false);
  apply_error_override(&result.error, message, stage);
  return result;
}

}  // namespace dasall::services::internal