#include "execution/ExecutionCommandLane.h"

#include <algorithm>
#include <functional>
#include <string_view>
#include <utility>

#include "bridges/ServiceAuditBridge.h"
#include "bridges/ServiceMetricsBridge.h"
#include "bridges/ServiceTraceBridge.h"
#include "execution/CompensationCatalog.h"

namespace dasall::services::internal {

namespace {

[[nodiscard]] bool contains_string(const std::vector<std::string>& values,
                                   const std::string_view& target) {
  return std::any_of(values.begin(), values.end(), [target](const std::string& value) {
    return value == target;
  });
}

[[nodiscard]] bool is_safe_mode_action(const std::string_view& action) {
  return action == "safe_mode.enter" || action == "safe_mode.exit";
}

[[nodiscard]] bool is_high_risk_action(const ExecutionCommandLaneDependencies& dependencies,
                                       const std::string_view& action) {
  return is_safe_mode_action(action) || contains_string(dependencies.high_risk_actions, action);
}

[[nodiscard]] bool is_critical_action(const ExecutionCommandLaneDependencies& dependencies,
                                      const std::string_view& action,
                                      bool high_risk_action) {
  return high_risk_action || contains_string(dependencies.critical_actions, action);
}

[[nodiscard]] std::string make_error_payload(std::string_view error_code,
                                             std::string_view message) {
  return std::string("{\"error\":\"") + std::string(error_code) +
         "\",\"message\":\"" + std::string(message) + "\"}";
}

[[nodiscard]] std::string make_idempotency_cache_key(const CapabilityTargetRef& target,
                                                     std::string_view action,
                                                     std::string_view idempotency_key) {
  return target.capability_id + ":" + target.target_id + ":" + std::string(action) + ":" +
         std::string(idempotency_key);
}

[[nodiscard]] std::string make_serialization_key(const CapabilityTargetRef& target,
                                                 std::string_view action) {
  return target.capability_id + ":" + target.target_id + ":" + std::string(action);
}

[[nodiscard]] std::string default_execution_id(const ExecutionCommandRequest& request) {
  if (request.idempotency_key.has_value()) {
    return "exec:" + *request.idempotency_key;
  }

  return "exec:" + request.context.request_id + ":" + request.action;
}

[[nodiscard]] std::string default_compensation_execution_id(
    const ExecutionCompensationRequest& request) {
  return "comp:" + request.source_execution_id + ":" + request.compensation_action;
}

[[nodiscard]] CapabilityRouteView resolve_route_view(
    const ExecutionCommandLaneDependencies& dependencies,
    const std::string& capability_id) {
  if (dependencies.resolve_route_view) {
    return dependencies.resolve_route_view(capability_id, AdapterRouteRequestKind::action);
  }

  return CapabilityRouteView{
      .capability_snapshot = dependencies.capability_snapshot,
      .registered_candidates = dependencies.registered_candidates,
  };
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

[[nodiscard]] AdapterRouteRequest build_route_request(const ServicePolicyView& policy_view,
                                                      const CapabilitySnapshotView& snapshot,
                                                      const FallbackEnvelope& fallback_envelope,
                                                      const std::vector<AdapterCandidateView>& candidates,
                                                      const CapabilityTargetRef& target,
                                                      std::string requested_operation,
                                                      bool high_risk_action) {
  return AdapterRouteRequest{
      .capability_id = target.capability_id,
      .target_id = target.target_id,
      .request_kind = AdapterRouteRequestKind::action,
      .requested_operation = std::move(requested_operation),
      .high_risk = high_risk_action,
      .minimum_trust = high_risk_action ? AdapterTrustClass::caller_verified
                                        : AdapterTrustClass::untrusted,
      .policy_view = policy_view,
      .capability_snapshot = snapshot,
      .fallback_envelope = fallback_envelope,
      .registered_candidates = candidates,
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

class ScopeExit {
 public:
  explicit ScopeExit(std::function<void()> on_exit) : on_exit_(std::move(on_exit)) {}

  ScopeExit(const ScopeExit&) = delete;
  ScopeExit& operator=(const ScopeExit&) = delete;

  ~ScopeExit() {
    if (on_exit_) {
      on_exit_();
    }
  }

 private:
  std::function<void()> on_exit_;
};

}  // namespace

ExecutionCommandLane::ExecutionCommandLane(ExecutionCommandLaneDependencies dependencies)
    : dependencies_(std::move(dependencies)) {}

ExecutionCommandResult ExecutionCommandLane::execute(const ServiceCallContext& context,
                                                     const ExecutionCommandRequest& request) {
  if (request.target.capability_id.empty() || request.target.target_id.empty() ||
      request.action.empty()) {
    auto result = make_error_result(request.target.target_id,
                                    "validator:" + context.request_id,
                                    "invalid_request",
                                    "capability_id, target_id, and action are required",
                                    "execution_command_lane",
                                    {},
                                    {});
    if (dependencies_.metrics_bridge != nullptr) {
      (void)dependencies_.metrics_bridge->record_execution_result(
          request.action,
          {},
          result,
          std::nullopt);
    }
    return result;
  }

  const auto high_risk_action = is_high_risk_action(dependencies_, request.action);
  const auto critical_action = is_critical_action(dependencies_, request.action, high_risk_action);

  if (critical_action && !request.idempotency_key.has_value()) {
    auto result = make_error_result(request.target.target_id,
                                    "validator:" + context.request_id,
                                    "invalid_request",
                                    "critical actions require idempotency_key",
                                    "execution_command_lane",
                                    {},
                                    {});
    if (dependencies_.metrics_bridge != nullptr) {
      (void)dependencies_.metrics_bridge->record_execution_result(
          request.action,
          {},
          result,
          std::nullopt);
    }
    return result;
  }

  const auto execution_id = dependencies_.make_execution_id
                                ? dependencies_.make_execution_id(request)
                                : default_execution_id(request);
  const auto idempotency_cache_key = request.idempotency_key.has_value()
                                         ? make_idempotency_cache_key(request.target,
                                                                      request.action,
                                                                      *request.idempotency_key)
                                         : std::string{};
  const auto audit_required = high_risk_action;
  if (audit_required && dependencies_.audit_bridge != nullptr) {
    (void)dependencies_.audit_bridge->write_execution_requested(
        context,
        request.target,
        request.action,
        execution_id,
        dependencies_.policy_view.high_risk_confirmation_required);
  }

  auto result = execute_impl(context,
                             request.target,
                             request.action,
                             request.arguments_json,
                             execution_id,
                             idempotency_cache_key,
                             critical_action,
                             high_risk_action,
                             audit_required);

  if (audit_required && dependencies_.audit_bridge != nullptr) {
    (void)dependencies_.audit_bridge->write_execution_completed(
        context,
        request.target,
        request.action,
        result);
  }

  return result;
}

ExecutionCommandResult ExecutionCommandLane::compensate(
    const ServiceCallContext& context,
    const ExecutionCompensationRequest& request) {
  if (request.target.capability_id.empty() || request.target.target_id.empty() ||
      request.compensation_action.empty() || request.source_execution_id.empty() ||
      request.reason_code.empty()) {
    auto result = make_error_result(request.target.target_id,
                                    "validator:" + context.request_id,
                                    "invalid_request",
                                    "compensation requires capability_id, target_id, compensation_action, source_execution_id, and reason_code",
                                    "execution_command_lane",
                                    {},
                                    {});
    if (dependencies_.metrics_bridge != nullptr) {
      (void)dependencies_.metrics_bridge->record_execution_result(
          request.compensation_action,
          {},
          result,
          std::nullopt);
    }
    return result;
  }

  const auto execution_id = dependencies_.make_compensation_execution_id
                                ? dependencies_.make_compensation_execution_id(request)
                                : default_compensation_execution_id(request);
  const auto idempotency_cache_key = make_idempotency_cache_key(
      request.target, request.compensation_action, request.source_execution_id);
  if (dependencies_.audit_bridge != nullptr) {
    (void)dependencies_.audit_bridge->write_compensation_requested(
        context,
        request,
        execution_id);
  }

  auto result = execute_impl(context,
                             request.target,
                             request.compensation_action,
                             request.arguments_json,
                             execution_id,
                             idempotency_cache_key,
                             true,
                             false,
                             true);

  if (dependencies_.audit_bridge != nullptr) {
    (void)dependencies_.audit_bridge->write_compensation_completed(
        context,
        request,
        result);
  }

  return result;
}

ExecutionCommandResult ExecutionCommandLane::execute_impl(const ServiceCallContext& context,
                                                          const CapabilityTargetRef& target,
                                                          const std::string& action,
                                                          const std::string& payload_json,
                                                          const std::string& execution_id,
                                                          const std::string& idempotency_cache_key,
                                                          bool critical_action,
                                                          bool high_risk_action,
                                                          bool audit_required) {
  auto invoke_lane = [&]() -> ExecutionCommandResult {
  if (dependencies_.router == nullptr || dependencies_.bridge == nullptr ||
      dependencies_.result_mapper == nullptr) {
    auto result = make_runtime_failure("command lane dependencies are not configured",
                                       "execution_command_lane",
                                       "command_lane.dependencies");
    if (dependencies_.metrics_bridge != nullptr) {
      (void)dependencies_.metrics_bridge->record_execution_result(
          action,
          {},
          result,
          std::nullopt);
    }
    return result;
  }

  if (!idempotency_cache_key.empty()) {
    ExecutionCommandResult cached_result;
    if (try_get_cached_result(idempotency_cache_key, &cached_result)) {
      if (dependencies_.metrics_bridge != nullptr) {
        (void)dependencies_.metrics_bridge->record_execution_result(
            action,
            {},
            cached_result,
            std::nullopt);
      }
      return cached_result;
    }
  }

  if (high_risk_action && !dependencies_.allow_high_risk_actions) {
    auto result = make_error_result(target.target_id,
                                    "policy:cap-gate-08",
                                    "policy_denied",
                                    "high-risk action remains gated until CAP-GATE-08 is satisfied",
                                    "execution_command_lane",
                                    {"cap-gate-08"},
                                    execution_id);
    if (dependencies_.metrics_bridge != nullptr) {
      (void)dependencies_.metrics_bridge->record_execution_result(
          action,
          {},
          result,
          std::nullopt);
    }
    return result;
  }

  const auto serialization_key = critical_action ? make_serialization_key(target, action)
                                                 : std::string{};
  if (!serialization_key.empty() && !try_acquire_serialization_key(serialization_key)) {
    auto result = make_error_result(target.target_id,
                                    "target-lease:" + serialization_key,
                                    "target_busy",
                                    "critical action is already in progress for target",
                                    "execution_command_lane",
                                    {},
                                    execution_id);
    if (dependencies_.metrics_bridge != nullptr) {
      (void)dependencies_.metrics_bridge->record_execution_result(
          action,
          {},
          result,
          std::nullopt);
    }
    return result;
  }
  ScopeExit releaser([this, serialization_key]() {
    if (!serialization_key.empty()) {
      release_serialization_key(serialization_key);
    }
  });

  if (!serialization_key.empty() && dependencies_.on_serialization_acquired) {
    dependencies_.on_serialization_acquired(serialization_key);
  }

  const auto route_view = resolve_route_view(dependencies_, target.capability_id);
  const auto route_request = build_route_request(dependencies_.policy_view,
                                                 route_view.capability_snapshot,
                                                 dependencies_.fallback_envelope,
                                                 route_view.registered_candidates,
                                                 target,
                                                 action,
                                                 high_risk_action);
  const auto route_decision = dependencies_.router->select_adapter(route_request);
  if (!route_decision.ok()) {
    auto result = make_error_result(target.target_id,
                    "route:" + context.request_id,
                    std::string(route_failure_name(route_decision.failure)),
                    route_decision.reason,
                    "adapter_router",
                    route_decision.failure ==
                        AdapterRouteFailure::route_not_permitted
                      ? std::vector<std::string>{"policy://route/" +
                                     context.request_id}
                      : std::vector<std::string>{},
                    execution_id);

    if (audit_required && dependencies_.audit_bridge != nullptr &&
      route_decision.failure == AdapterRouteFailure::fallback_blocked) {
      (void)dependencies_.audit_bridge->write_fallback_blocked(
        context,
        target,
        action,
        execution_id,
        route_decision.reason,
        dependencies_.fallback_envelope.requested_action_class);
    }

    if (dependencies_.metrics_bridge != nullptr) {
      if (route_decision.failure == AdapterRouteFailure::route_unavailable) {
        (void)dependencies_.metrics_bridge->record_execution_circuit_open(
            action,
            {},
            route_decision.reason);
      }
      (void)dependencies_.metrics_bridge->record_execution_result(
          action,
          {},
          result,
          std::nullopt);
    }

    return result;
  }

  const auto receipt = dependencies_.bridge->invoke(
      *route_decision.selection,
      AdapterInvocationRequest{
          .request_id = context.request_id,
          .capability_id = target.capability_id,
          .target_id = target.target_id,
          .request_kind = AdapterRouteRequestKind::action,
          .operation_name = action,
          .payload_json = payload_json,
      });

    const auto compensation_hints = dependencies_.lookup_compensation_hints
                ? dependencies_.lookup_compensation_hints(
                  target.capability_id,
                  action,
                  route_view.capability_snapshot.capability_version,
                  receipt)
                : (dependencies_.compensation_catalog != nullptr
                   ? dependencies_.compensation_catalog->flatten_hints(
                     dependencies_.compensation_catalog->lookup(
                     target.capability_id,
                     action,
                     route_view.capability_snapshot
                         .capability_version))
                   : std::vector<std::string>{});
  auto result = dependencies_.result_mapper->to_execution_command_result(
      receipt,
      compensation_hints,
      execution_id);

  if (result.error.has_value() && !receipt.provider_status_code.empty()) {
    result.error->details.stage = "execution_command_lane";
  }

  if (!idempotency_cache_key.empty()) {
    cache_result(idempotency_cache_key, result);
  }

  if (dependencies_.metrics_bridge != nullptr) {
    (void)dependencies_.metrics_bridge->record_execution_result(
        action,
        route_decision.selection->adapter_id,
        result,
        receipt.latency_ms);
  }

  return result;
  };

  if (dependencies_.trace_bridge == nullptr) {
    return invoke_lane();
  }

  auto span = dependencies_.trace_bridge->start_lane_span(
      context,
      "execution.command",
      action,
      &target);
  auto result = dependencies_.trace_bridge->with_span(span, invoke_lane);
  dependencies_.trace_bridge->complete_span(&span, result);
  return result;
}

ExecutionCommandResult ExecutionCommandLane::make_runtime_failure(const std::string& message,
                                                                  const std::string& stage,
                                                                  const std::string& ref_id) const {
  return ExecutionCommandResult{
      .code = contracts::ResultCode::RuntimeRetryExhausted,
      .execution_id = {},
      .payload_json = {},
      .side_effects = {},
      .compensation_hints = {},
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

ExecutionCommandResult ExecutionCommandLane::make_error_result(
    const std::string& target_id,
    const std::string& receipt_ref,
    const std::string& provider_status_code,
    const std::string& message,
    const std::string& stage,
    std::vector<std::string> evidence_refs,
    std::string execution_id) const {
  if (dependencies_.result_mapper == nullptr) {
    return make_runtime_failure(message, stage, receipt_ref);
  }

  auto result = dependencies_.result_mapper->to_execution_command_result(
      make_receipt(receipt_ref,
                   target_id,
                   provider_status_code,
                   make_error_payload(provider_status_code, message),
                   std::move(evidence_refs)),
      {},
      std::move(execution_id));
  apply_error_override(&result.error, message, stage);
  return result;
}

void ExecutionCommandLane::cache_result(const std::string& cache_key,
                                        const ExecutionCommandResult& result) {
  std::lock_guard<std::mutex> lock(state_mutex_);
  idempotency_cache_[cache_key] = result;
}

bool ExecutionCommandLane::try_get_cached_result(const std::string& cache_key,
                                                 ExecutionCommandResult* result) const {
  std::lock_guard<std::mutex> lock(state_mutex_);
  const auto it = idempotency_cache_.find(cache_key);
  if (it == idempotency_cache_.end()) {
    return false;
  }

  *result = it->second;
  return true;
}

bool ExecutionCommandLane::try_acquire_serialization_key(const std::string& serialization_key) {
  std::lock_guard<std::mutex> lock(state_mutex_);
  return in_flight_serialization_keys_.insert(serialization_key).second;
}

void ExecutionCommandLane::release_serialization_key(const std::string& serialization_key) {
  std::lock_guard<std::mutex> lock(state_mutex_);
  in_flight_serialization_keys_.erase(serialization_key);
}

}  // namespace dasall::services::internal