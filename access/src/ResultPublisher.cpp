#include "ResultPublisher.h"

#include <optional>
#include <string>
#include <utility>

namespace dasall::access {

ResultPublisher::ResultPublisher(EmitBackend emit_backend)
    : emit_backend_(std::move(emit_backend)) {}

PublishEnvelope ResultPublisher::build_envelope(
    const RuntimeDispatchRequest& request,
    const dasall::contracts::AgentResult& agent_result) const {
  PublishEnvelope envelope;

  envelope.request_id =
      context_value(request, "request_id").value_or(request.packet.packet_id);
  envelope.result_id = agent_result.result_id.value_or("result:" + envelope.request_id);
  envelope.session_id =
      context_value(request, "session_id").value_or("session:" + envelope.request_id);
  envelope.trace_id = context_value(request, "trace_id").value_or("trace:" + envelope.request_id);
  envelope.channel_ref = request.packet.entry_type + "://" + request.packet.protocol_kind;
  envelope.protocol_kind = request.packet.protocol_kind;
  envelope.agent_result = agent_result;

  const auto protocol_projection = map_protocol_status(agent_result);
  envelope.protocol_status_hint = std::to_string(protocol_projection.http_status);
  envelope.protocol_metadata = std::string(protocol_projection.grpc_status);

  // status=Completed 或 task_completed=true 视为 final；其余场景可由上游继续补偿发布。
  envelope.is_final =
      (agent_result.status.has_value() &&
       *agent_result.status == dasall::contracts::AgentResultStatus::Completed) ||
      (agent_result.task_completed.has_value() && *agent_result.task_completed);

  envelope.payload = agent_result.response_text.value_or(std::string());
  return envelope;
}

AccessProtocolErrorMapping ResultPublisher::map_protocol_status(
    const dasall::contracts::AgentResult& agent_result) const {
  return map_agent_result_to_protocol(agent_result);
}

bool ResultPublisher::emit_publish(const PublishEnvelope& envelope) const {
  if (!emit_backend_) {
    return false;
  }

  return emit_backend_(envelope);
}

PublishAttemptResult ResultPublisher::publish(
    const RuntimeDispatchRequest& request,
    const dasall::contracts::AgentResult& agent_result) const {
  PublishAttemptResult attempt;
  attempt.envelope = build_envelope(request, agent_result);
  attempt.published = emit_publish(attempt.envelope);

  if (!attempt.published) {
    attempt.error = make_access_error(
        AccessErrorCode::PublishChannelUnavailable,
        "result publisher failed to emit publish envelope",
        std::nullopt,
        "publish_channel_unavailable");
  }

  return attempt;
}

std::optional<std::string> ResultPublisher::context_value(
    const RuntimeDispatchRequest& request,
    const std::string& key) {
  const auto it = request.request_context.find(key);
  if (it == request.request_context.end() || it->second.empty()) {
    return std::nullopt;
  }

  return it->second;
}

}  // namespace dasall::access
