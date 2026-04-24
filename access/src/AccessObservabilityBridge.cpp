#include "AccessObservabilityBridge.h"

#include <utility>

namespace dasall::access {

namespace {

[[nodiscard]] std::string context_or_default(
    const RuntimeDispatchRequest& request,
    const std::string& key,
    const std::string& fallback) {
  const auto it = request.request_context.find(key);
  if (it == request.request_context.end() || it->second.empty()) {
    return fallback;
  }

  return it->second;
}

}  // namespace

AccessObservabilityBridge::AccessObservabilityBridge(EmitBackend backend)
    : backend_(std::move(backend)) {}

bool AccessObservabilityBridge::emit_request_received(
    const InboundPacket& packet,
    const std::string_view request_id,
    const std::string_view session_id,
    const std::string_view trace_id,
    const std::optional<std::string_view> actor_ref) const {
  AccessObservabilityEvent event;
  event.name = "access.request.received";
  event.fields["request_id"] = std::string(request_id);
  event.fields["session_id"] = std::string(session_id);
  event.fields["trace_id"] = std::string(trace_id);
  event.fields["entry_type"] = packet.entry_type;
  event.fields["protocol_kind"] = packet.protocol_kind;
  event.fields["peer_ref"] = packet.peer_ref;
  if (actor_ref.has_value() && !actor_ref->empty()) {
    event.fields["actor_ref"] = std::string(*actor_ref);
  }
  return emit_event(std::move(event));
}

bool AccessObservabilityBridge::emit_auth_failed(
    const InboundPacket& packet,
    const std::string_view request_id,
    const std::string_view trace_id,
    const std::string_view reason_code,
    const std::optional<std::string_view> actor_ref) const {
  AccessObservabilityEvent event;
  event.name = "access.auth.failed";
  event.fields["request_id"] = std::string(request_id);
  event.fields["trace_id"] = std::string(trace_id);
  event.fields["entry_type"] = packet.entry_type;
  event.fields["protocol_kind"] = packet.protocol_kind;
  event.fields["reason_code"] = std::string(reason_code);
  event.fields["peer_ref"] = packet.peer_ref;
  if (actor_ref.has_value() && !actor_ref->empty()) {
    event.fields["actor_ref"] = std::string(*actor_ref);
  }
  return emit_event(std::move(event));
}

bool AccessObservabilityBridge::emit_policy_denied(
    const RuntimeDispatchRequest& request,
    const std::string_view reason_code) const {
  AccessObservabilityEvent event;
  event.name = "access.policy.denied";
  event.fields["request_id"] = context_or_default(request, "request_id", request.packet.packet_id);
  event.fields["session_id"] = context_or_default(request, "session_id", "");
  event.fields["trace_id"] = context_or_default(request, "trace_id", "");
  event.fields["actor_ref"] = request.subject_identity.actor_ref;
  event.fields["operation"] = context_or_default(request, "operation", "access.request.submit");
  event.fields["target_type"] = context_or_default(request, "target_type", "access.entry");
  event.fields["reason_code"] = std::string(reason_code);
  event.fields["policy_decision_ref"] = request.decision_proof.policy_decision_ref;
  return emit_event(std::move(event));
}

bool AccessObservabilityBridge::emit_dispatch_result(
    const RuntimeDispatchRequest& request,
    const RuntimeDispatchResult& result,
    const std::int64_t latency_ms) const {
  AccessObservabilityEvent event;
  event.name = "access.runtime.dispatched";
  event.fields["request_id"] = context_or_default(request, "request_id", request.packet.packet_id);
  event.fields["session_id"] = context_or_default(request, "session_id", "");
  event.fields["trace_id"] = context_or_default(request, "trace_id", "");
  event.fields["entry_type"] = request.packet.entry_type;
  event.fields["disposition"] = std::to_string(static_cast<int>(result.disposition));
  event.fields["runtime_latency_ms"] = std::to_string(latency_ms);
  if (result.error_ref.has_value()) {
    event.fields["error_ref"] = *result.error_ref;
  }
  return emit_event(std::move(event));
}

bool AccessObservabilityBridge::emit_publish_failed(
    const PublishEnvelope& envelope,
    const AccessErrorCode error_code,
    const std::string_view detail) const {
  AccessObservabilityEvent event;
  event.name = "access.publish.failed";
  event.fields["request_id"] = envelope.request_id;
  event.fields["session_id"] = envelope.session_id;
  event.fields["trace_id"] = envelope.trace_id;
  event.fields["result_id"] = envelope.result_id;
  event.fields["protocol_kind"] = envelope.protocol_kind;
  event.fields["error_code"] = std::to_string(static_cast<int>(error_code));
  event.fields["detail"] = std::string(detail);
  return emit_event(std::move(event));
}

bool AccessObservabilityBridge::emit_event(AccessObservabilityEvent event) const {
  if (!backend_) {
    return false;
  }

  // 观测发送失败只影响可观测性，不应改变业务路径。
  return backend_(event);
}

}  // namespace dasall::access
