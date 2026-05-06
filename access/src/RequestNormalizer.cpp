#include "RequestNormalizer.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace dasall::access {

namespace {

[[nodiscard]] bool has_value(const std::optional<std::string>& value) {
  return value.has_value() && !value->empty();
}

}  // namespace

RequestNormalizer::RequestNormalizer(AccessBootstrapConfig bootstrap_config,
                                     AccessPublishView publish_view)
    : bootstrap_config_(std::move(bootstrap_config)),
      publish_view_(std::move(publish_view)) {}

RequestNormalizationOutput RequestNormalizer::normalize(
    const RuntimeDispatchRequest& request) const {
  RequestNormalizationOutput output;
  output.runtime_request = request;

  // normalize 阶段是 RuntimeBridge 的前置条件，必要字段缺失必须 fail-closed。
  if (request.packet.packet_id.empty() || request.packet.entry_type.empty() ||
      request.packet.protocol_kind.empty()) {
    output.error = make_access_error(
        AccessErrorCode::ValidationRejected,
        "request normalizer requires packet_id/entry_type/protocol_kind",
        std::nullopt,
        "normalizer_missing_packet_metadata");
    return output;
  }

  if (request.subject_identity.actor_ref.empty()) {
    output.error = make_access_error(
        AccessErrorCode::ValidationRejected,
        "request normalizer requires non-empty actor_ref",
        std::nullopt,
        "normalizer_missing_subject_actor_ref");
    return output;
  }

  const TraceIdentityBundle ids = ensure_trace_ids(request);
  output.agent_request = project_agent_request(request, ids);
  output.publish_context = build_publish_context(request, ids);
  output.runtime_request.agent_request = output.agent_request;

  output.runtime_request.request_context["request_id"] = ids.request_id;
  output.runtime_request.request_context["session_id"] = ids.session_id;
  output.runtime_request.request_context["trace_id"] = ids.trace_id;

  // 只投影白名单约束，防止把入口随意上下文污染到 shared contracts。
  if (const auto constraint_set = context_value(request, "constraint_set");
      has_value(constraint_set)) {
    output.runtime_request.request_context["constraint_set"] = *constraint_set;
  }

  output.runtime_request.request_context["normalizer_ready"] = "true";
  output.normalized = true;
  return output;
}

RequestNormalizer::TraceIdentityBundle RequestNormalizer::ensure_trace_ids(
    const RuntimeDispatchRequest& request) const {
  TraceIdentityBundle ids;

  const auto request_id = context_value(request, "request_id");
  const auto session_id = context_value(request, "session_id");
  const auto trace_id = context_value(request, "trace_id");

  ids.request_id = has_value(request_id)
                       ? *request_id
                       : generate_stable_id("req", request, 1);
  ids.session_id = has_value(session_id)
                       ? *session_id
                       : generate_stable_id("sess", request, 2);
  ids.trace_id = has_value(trace_id)
                     ? *trace_id
                     : generate_stable_id("trace", request, 3);

  return ids;
}

dasall::contracts::AgentRequest RequestNormalizer::project_agent_request(
    const RuntimeDispatchRequest& request,
    const TraceIdentityBundle& ids) const {
  dasall::contracts::AgentRequest agent_request;

  agent_request.request_id = ids.request_id;
  agent_request.session_id = ids.session_id;
  agent_request.trace_id = ids.trace_id;
  agent_request.user_input = request.packet.payload;
  agent_request.request_channel = map_request_channel(request.packet.entry_type);
  agent_request.created_at = now_epoch_millis();

  agent_request.goal_hint = context_value(request, "goal_hint");
  agent_request.domain_context = context_value(request, "domain_context");
  agent_request.constraint_set = context_value(request, "constraint_set");
  agent_request.approval_policy_hint =
      context_value(request, "approval_policy_hint");
  agent_request.idempotency_key = context_value(request, "idempotency_key");
  agent_request.locale = context_value(request, "locale");

  if (!request.client_capability_view.empty()) {
    agent_request.client_capabilities = request.client_capability_view;
  }

  // deadline_at 优先由 access_deadline 或 dispatch_deadline_ms 决定。
  if (request.access_deadline.has_value() && !request.access_deadline->empty()) {
    try {
      agent_request.deadline_at = std::stoll(*request.access_deadline);
    } catch (...) {
      // 非法 deadline 字符串不应阻断归一化主流程，降级使用 timeout 策略。
      agent_request.deadline_at = std::nullopt;
    }
  }

  if (!agent_request.deadline_at.has_value() &&
      bootstrap_config_.dispatch_deadline_ms > 0) {
    agent_request.timeout_ms =
        static_cast<std::uint32_t>(bootstrap_config_.dispatch_deadline_ms);
  }

  return agent_request;
}

PublishEnvelope RequestNormalizer::build_publish_context(
    const RuntimeDispatchRequest& request,
    const TraceIdentityBundle& ids) const {
  PublishEnvelope envelope;
  envelope.request_id = ids.request_id;
  envelope.result_id = "pending-result:" + ids.request_id;
  envelope.session_id = ids.session_id;
  envelope.trace_id = ids.trace_id;
  envelope.channel_ref = request.packet.entry_type + "://" + request.packet.protocol_kind;
  envelope.protocol_kind = request.packet.protocol_kind;
  envelope.protocol_status_hint = "202 Accepted";
  envelope.protocol_metadata = "normalizer_seed";
  envelope.is_final = false;
  envelope.payload = std::string();
  return envelope;
}

std::optional<std::string> RequestNormalizer::context_value(
    const RuntimeDispatchRequest& request,
    const std::string& key) {
  const auto it = request.request_context.find(key);
  if (it == request.request_context.end() || it->second.empty()) {
    return std::nullopt;
  }

  return it->second;
}

dasall::contracts::RequestChannel RequestNormalizer::map_request_channel(
    const std::string& entry_type) {
  if (entry_type == "cli") {
    return dasall::contracts::RequestChannel::Cli;
  }

  if (entry_type == "gateway") {
    return dasall::contracts::RequestChannel::Gateway;
  }

  if (entry_type == "daemon") {
    return dasall::contracts::RequestChannel::Daemon;
  }

  if (entry_type == "simulator") {
    return dasall::contracts::RequestChannel::Simulator;
  }

  return dasall::contracts::RequestChannel::Unspecified;
}

std::int64_t RequestNormalizer::now_epoch_millis() {
  const auto now = std::chrono::system_clock::now();
  const auto millis =
      std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
  return millis.count();
}

std::string RequestNormalizer::generate_stable_id(
    const std::string& prefix,
    const RuntimeDispatchRequest& request,
    const std::size_t ordinal) const {
  const std::size_t counter = id_counter_.fetch_add(1, std::memory_order_relaxed) + 1;
  return prefix + ":" + request.packet.packet_id + ":" + std::to_string(ordinal) + ":" +
         std::to_string(counter);
}

}  // namespace dasall::access
