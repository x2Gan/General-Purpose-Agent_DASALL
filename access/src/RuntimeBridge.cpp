#include "RuntimeBridge.h"

#include <string>
#include <utility>

#include "agent/AgentRequestGuards.h"

namespace dasall::access {

namespace {

[[nodiscard]] std::optional<std::string> agent_request_value(
    const RuntimeDispatchRequest& request,
    const std::string_view key) {
  if (key == "request_id" && request.agent_request.request_id.has_value() &&
      !request.agent_request.request_id->empty()) {
    return request.agent_request.request_id;
  }
  if (key == "session_id" && request.agent_request.session_id.has_value() &&
      !request.agent_request.session_id->empty()) {
    return request.agent_request.session_id;
  }
  if (key == "trace_id" && request.agent_request.trace_id.has_value() &&
      !request.agent_request.trace_id->empty()) {
    return request.agent_request.trace_id;
  }
  if (key == "idempotency_key" &&
      request.agent_request.idempotency_key.has_value() &&
      !request.agent_request.idempotency_key->empty()) {
    return *request.agent_request.idempotency_key;
  }

  return std::nullopt;
}

}  // namespace

RuntimeBridge::RuntimeBridge(DispatchBackend dispatch_backend, CancelBackend cancel_backend)
    : dispatch_backend_(std::move(dispatch_backend)),
      cancel_backend_(std::move(cancel_backend)) {}

RuntimeDispatchResult RuntimeBridge::dispatch(const RuntimeDispatchRequest& request) {
  // RuntimeBridge 是 access -> runtime 唯一调用缝；归一化未完成时必须 fail-closed。
  const auto normalizer_ready = context_value(request, "normalizer_ready");
  if (!normalizer_ready.has_value() || *normalizer_ready != "true") {
    return map_runtime_reject(
        AccessErrorCode::ValidationRejected,
        "runtime bridge requires normalizer_ready=true",
        std::string("normalizer gate is not satisfied"));
  }

  if (request.decision_proof.decision != "Allow") {
    return map_runtime_reject(
        AccessErrorCode::AuthorizationDenied,
        "runtime bridge only dispatches Allow decision requests",
        std::string("decision_proof is not allow"));
  }

  const auto agent_request_guard =
      dasall::contracts::validate_agent_request_field_rules(request.agent_request);
  if (!agent_request_guard.ok) {
    return map_runtime_reject(
        AccessErrorCode::ValidationRejected,
        "runtime bridge requires valid public AgentRequest handoff",
        agent_request_guard.reason.empty()
          ? std::optional<std::string>("agent_request field guard failed")
          : std::optional<std::string>(std::string(agent_request_guard.reason)));
  }

  if (!dispatch_backend_) {
    return map_runtime_reject(
        AccessErrorCode::RuntimeBridgeUnavailable,
        "runtime bridge backend is not configured",
        std::string("dispatch backend is empty"));
  }

  const RuntimeDispatchResult backend_result = dispatch_backend_(request);
  return map_runtime_result(request, backend_result);
}

bool RuntimeBridge::cancel(const std::string_view request_id,
                           const std::string_view actor_ref) {
  if (!cancel_backend_ || request_id.empty() || actor_ref.empty()) {
    return false;
  }

  // cancel 只做转发，不在 access 层进行最终裁定。
  return cancel_backend_(request_id, actor_ref);
}

RuntimeDispatchResult RuntimeBridge::map_runtime_result(
    const RuntimeDispatchRequest& request,
    const RuntimeDispatchResult& backend_result) const {
  RuntimeDispatchResult mapped = backend_result;

  // 统一补齐追踪上下文，避免 runtime 返回分支丢失 request/session/trace。
  if (const auto request_id = agent_request_value(request, "request_id");
      request_id.has_value()) {
    mapped.response_context["request_id"] = *request_id;
  }
  if (const auto session_id = agent_request_value(request, "session_id");
      session_id.has_value()) {
    mapped.response_context["session_id"] = *session_id;
  }
  if (const auto trace_id = agent_request_value(request, "trace_id");
      trace_id.has_value()) {
    mapped.response_context["trace_id"] = *trace_id;
  }

  if (mapped.disposition == AccessDisposition::AcceptedAsync &&
      !mapped.receipt_ref.has_value()) {
    const auto fallback_request_id =
        agent_request_value(request, "request_id").value_or(request.packet.packet_id);
    mapped.receipt_ref = std::string("receipt:") + fallback_request_id;
  }

  return mapped;
}

RuntimeDispatchResult RuntimeBridge::map_runtime_reject(
    const AccessErrorCode error_code,
    std::string reason,
    std::optional<std::string> detail) const {
  RuntimeDispatchResult result;
  result.disposition = AccessDisposition::Rejected;
  result.error_ref = std::move(reason);
  result.response_context["error_code"] = std::to_string(static_cast<int>(error_code));
  if (detail.has_value()) {
    result.response_context["error_detail"] = std::move(*detail);
  }
  return result;
}

std::optional<std::string> RuntimeBridge::context_value(
    const RuntimeDispatchRequest& request,
    const std::string& key) {
  if (const auto public_value = agent_request_value(request, key);
      public_value.has_value()) {
    return public_value;
  }

  const auto it = request.request_context.find(key);
  if (it == request.request_context.end() || it->second.empty()) {
    return std::nullopt;
  }

  return it->second;
}

}  // namespace dasall::access
