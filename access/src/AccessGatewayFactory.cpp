#include "AccessGatewayFactory.h"

#include "AccessConfigAdapter.h"

#include <cctype>
#include <chrono>
#include <charconv>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "AccessGateway.h"
#include "AccessObservabilityBridge.h"
#include "AccessPolicyGate.h"
#include "AdmissionController.h"
#include "AsyncTaskRegistry.h"
#include "IKnowledgeService.h"
#include "KnowledgeTypes.h"
#include "AuthenticatorChain.h"
#include "RequestNormalizer.h"
#include "RequestValidator.h"
#include "ResultPublisher.h"
#include "RuntimeBridge.h"
#include "SubjectResolver.h"
#include "daemon/DaemonDiagnosticsHandler.h"
#include "daemon/DaemonHealthService.h"
#include "daemon/DaemonProtocolTypes.h"
#include "daemon/DaemonResponseBuilderWithReceipt.h"
#include "daemon/DaemonTaskQueryHandler.h"
#include "secret/ISecretManager.h"

namespace dasall::access {

namespace {

template <typename PipelineOptions>
[[nodiscard]] bool project_access_views_from_runtime_policy(
    PipelineOptions& options) {
  if (!options.derive_views_from_runtime_policy) {
    return true;
  }

  if (!options.runtime_policy_snapshot) {
    return false;
  }

  AccessConfigAdapter adapter;
  const auto projection =
      adapter.project(options.bootstrap_config, *options.runtime_policy_snapshot);
  if (!projection.ok() || !projection.projection.has_value()) {
    return false;
  }

  options.auth_view = projection.projection->auth_view;
  options.admission_view = projection.projection->admission_view;
  options.publish_view = projection.projection->publish_view;
  return true;
}

[[nodiscard]] std::shared_ptr<AccessObservabilityBridge> make_observability_bridge(
    const AccessObservabilityEmitBackend& emit_backend) {
  if (!emit_backend) {
    return std::make_shared<AccessObservabilityBridge>();
  }

  return std::make_shared<AccessObservabilityBridge>(
      [emit_backend](const AccessObservabilityEvent& event) {
        return emit_backend(event.name, event.fields);
      });
}

[[nodiscard]] RuntimeDispatchResult make_rejected_result(AccessErrorCode error_code,
                                                         std::string error_ref) {
  RuntimeDispatchResult result;
  result.disposition = AccessDisposition::Rejected;
  result.error_ref = std::move(error_ref);
  result.response_context["error_code"] =
      std::to_string(static_cast<int>(error_code));
  return result;
}

[[nodiscard]] RuntimeDispatchResult make_replay_result(
    const std::optional<std::string>& replay_receipt_ref) {
  RuntimeDispatchResult result;
  result.disposition = AccessDisposition::AcceptedAsync;
  result.receipt_ref = replay_receipt_ref.value_or("idempotency-replay");
  return result;
}

[[nodiscard]] std::string secure_buffer_to_string(
    const dasall::infra::secret::SecureBuffer& buffer) {
  std::string value;
  value.reserve(buffer.size());
  for (const std::byte byte : buffer.bytes()) {
    value.push_back(static_cast<char>(byte));
  }
  return value;
}

[[nodiscard]] std::optional<std::string> normalize_secret_name(
    std::string_view secret_ref) {
  constexpr std::string_view kSecretRefPrefix = "secret://";
  if (!secret_ref.starts_with(kSecretRefPrefix) ||
      secret_ref.size() <= kSecretRefPrefix.size()) {
    return std::nullopt;
  }
  return std::string(secret_ref.substr(kSecretRefPrefix.size()));
}

[[nodiscard]] std::optional<AsyncTaskRegistry::OwnershipTokenKey>
materialize_async_ownership_key(
    dasall::infra::secret::ISecretManager& secret_manager,
    std::string_view ownership_secret_ref,
    std::string_view entry_type) {
  const auto secret_name = normalize_secret_name(ownership_secret_ref);
  if (!secret_name.has_value()) {
    return std::nullopt;
  }

  const auto access_context = dasall::infra::secret::SecretAccessContext{
      .request_id = std::string("access-startup:") + std::string(entry_type),
      .session_id = std::nullopt,
      .task_id = std::string("access-async-registry:") + std::string(entry_type),
      .actor = std::string(entry_type),
      .consumer_module = std::string("access.async_task_registry"),
      .permission_domain = std::string("secret.read"),
  };
  const auto handle_result = secret_manager.get_secret(
      dasall::infra::secret::SecretQuery{
          .secret_name = *secret_name,
          .version_hint = {},
          .purpose = std::string("access_async_ownership"),
          .access_mode = dasall::infra::secret::SecretAccessMode::Materialize,
      },
      access_context);
  if (!handle_result.ok || !handle_result.handle.is_valid()) {
    return std::nullopt;
  }

  const auto materialized_result =
      secret_manager.materialize(handle_result.handle, access_context);
  if (!materialized_result.ok || materialized_result.materialized_secret == nullptr ||
      !materialized_result.materialized_secret->is_accessible()) {
    return std::nullopt;
  }

  const auto secret_text =
      secure_buffer_to_string(*materialized_result.materialized_secret);
  (void)secret_manager.release(materialized_result.lease);
  AsyncTaskRegistry::OwnershipTokenKey key{
      .key_id = handle_result.handle.version,
      .secret = secret_text,
  };
  if (!key.is_valid()) {
    return std::nullopt;
  }

  return key;
}

template <typename PipelineOptions>
[[nodiscard]] std::shared_ptr<AsyncTaskRegistry> resolve_async_task_registry(
    const PipelineOptions& options) {
  if (options.async_task_registry) {
    return options.async_task_registry;
  }

  const auto ttl = std::chrono::milliseconds(options.bootstrap_config.result_replay_ttl_ms);
  if (!options.bootstrap_config.ownership_token_hmac_secret_ref.has_value() ||
      options.bootstrap_config.ownership_token_hmac_secret_ref->empty() ||
      options.ownership_secret_manager == nullptr) {
    return std::make_shared<AsyncTaskRegistry>(std::string{}, ttl);
  }

  const auto key = materialize_async_ownership_key(
      *options.ownership_secret_manager,
      *options.bootstrap_config.ownership_token_hmac_secret_ref,
      options.bootstrap_config.entry_type);
  if (!key.has_value()) {
    return std::make_shared<AsyncTaskRegistry>(std::string{}, ttl);
  }

  return std::make_shared<AsyncTaskRegistry>(*key, std::nullopt, ttl);
}

[[nodiscard]] std::string request_context_or_default(
    const RuntimeDispatchRequest& request,
    std::string_view key,
    std::string fallback) {
  if (key == "request_id" && request.agent_request.request_id.has_value() &&
      !request.agent_request.request_id->empty()) {
    return *request.agent_request.request_id;
  }
  if (key == "session_id" && request.agent_request.session_id.has_value() &&
      !request.agent_request.session_id->empty()) {
    return *request.agent_request.session_id;
  }
  if (key == "trace_id" && request.agent_request.trace_id.has_value() &&
      !request.agent_request.trace_id->empty()) {
    return *request.agent_request.trace_id;
  }

  const auto it = request.request_context.find(std::string(key));
  if (it == request.request_context.end() || it->second.empty()) {
    return fallback;
  }

  return it->second;
}

[[nodiscard]] RuntimeDispatchRequest build_policy_denied_observability_request(
    const InboundPacket& packet,
    const AuthenticationOutcome& auth_outcome,
    const AccessPolicyEvaluationResult& policy_result) {
  RuntimeDispatchRequest request;
  request.packet = packet;
  request.subject_identity = auth_outcome.subject_identity;
  request.decision_proof = policy_result.decision_proof;
  request.request_context["request_id"] = packet.packet_id;
  request.request_context["operation"] = "access.request.submit";
  request.request_context["target_type"] = "access.entry";
  if (packet.trace_id.has_value() && !packet.trace_id->empty()) {
    request.request_context["trace_id"] = *packet.trace_id;
  }
  if (packet.session_hint.has_value() && !packet.session_hint->empty()) {
    request.request_context["session_id"] = *packet.session_hint;
  }
  return request;
}

void project_packet_headers_to_request_context(const InboundPacket& packet,
                                               RuntimeDispatchRequest& request) {
  for (const auto& [key, value] : packet.headers) {
    if (!key.empty() && !value.empty()) {
      request.request_context[key] = value;
    }
  }
}

[[nodiscard]] LocalPeerUidFact parse_local_peer_fact(const InboundPacket& packet) {
  LocalPeerUidFact fact;

  constexpr std::string_view kLocalTrustedPrefix = "local_trusted:";
  if (packet.peer_ref.rfind(kLocalTrustedPrefix.data(), 0U) != 0U) {
    return fact;
  }

  fact.is_local_socket_peer = true;
  fact.eligible_for_local_trusted = true;

  const std::string_view uid_text =
      std::string_view(packet.peer_ref).substr(kLocalTrustedPrefix.size());
  std::uint32_t uid = 0U;
  const auto parse_result =
      std::from_chars(uid_text.data(), uid_text.data() + uid_text.size(), uid);
  if (parse_result.ec != std::errc{}) {
    fact.eligible_for_local_trusted = false;
    return fact;
  }

  fact.peer_uid = uid;
  fact.actor_ref = "local://uid/" + std::to_string(uid);
  return fact;
}

[[nodiscard]] PeerMetadata parse_gateway_peer_metadata(const InboundPacket& packet) {
  PeerMetadata metadata;

  constexpr std::string_view kJwtPrefix = "jwt:";
  constexpr std::string_view kTokenPrefix = "token:";
  constexpr std::string_view kMtlsPrefix = "mtls:";
  constexpr std::string_view kSimulatorPrefix = "simulator:";

  if (packet.peer_ref.rfind(kJwtPrefix.data(), 0U) == 0U) {
    metadata.jwt_actor_ref = packet.peer_ref.substr(kJwtPrefix.size());
    return metadata;
  }

  if (packet.peer_ref.rfind(kTokenPrefix.data(), 0U) == 0U) {
    metadata.token_actor_ref = packet.peer_ref.substr(kTokenPrefix.size());
    return metadata;
  }

  if (packet.peer_ref.rfind(kMtlsPrefix.data(), 0U) == 0U) {
    metadata.certificate_subject = packet.peer_ref.substr(kMtlsPrefix.size());
    return metadata;
  }

  if (packet.peer_ref.rfind(kSimulatorPrefix.data(), 0U) == 0U) {
    metadata.simulator_actor_ref = packet.peer_ref.substr(kSimulatorPrefix.size());
    return metadata;
  }

  return metadata;
}

[[nodiscard]] bool attach_async_receipt(
    RuntimeDispatchResult& dispatch_result,
    const RuntimeDispatchRequest& request,
    AsyncTaskRegistry& async_task_registry,
    AccessObservabilityBridge& observability_bridge) {
  if (dispatch_result.disposition != AccessDisposition::AcceptedAsync) {
    return true;
  }

  if (!async_task_registry.enabled()) {
    return false;
  }

  const auto receipt = async_task_registry.register_async_accept(request, dispatch_result);
  if (!receipt.has_value()) {
    return false;
  }

  const std::string request_id = request_context_or_default(
      request, "request_id", request.packet.packet_id);
  const std::string session_id = request_context_or_default(
      request, "session_id", "session:" + request_id);
  const std::string trace_id = request_context_or_default(
      request, "trace_id", "trace:" + request_id);

  dispatch_result.receipt_ref = receipt->receipt_id;
  if (!dispatch_result.publish_envelope.has_value()) {
    dispatch_result.publish_envelope = PublishEnvelope{};
  }
  dispatch_result.publish_envelope->request_id = request_id;
  dispatch_result.publish_envelope->result_id = receipt->receipt_id;
  dispatch_result.publish_envelope->session_id = session_id;
  dispatch_result.publish_envelope->trace_id = trace_id;
  dispatch_result.publish_envelope->channel_ref =
      request.packet.entry_type + "://" + request.packet.protocol_kind;
  dispatch_result.publish_envelope->protocol_kind = request.packet.protocol_kind;
  dispatch_result.publish_envelope->protocol_status_hint = "202";
  dispatch_result.publish_envelope->payload = "accepted_async";
  dispatch_result.publish_envelope->receipt = *receipt;
  (void)observability_bridge.emit_receipt_event(
      request_id,
      session_id,
      trace_id,
      "READY",
      receipt->receipt_id);
  return true;
}

struct DaemonStatusQueryPayload {
  std::string receipt_ref;
  std::string actor_ref;
  std::string ownership_token;
};

[[nodiscard]] std::optional<DaemonStatusQueryPayload> parse_status_query_payload(
    std::string_view payload) {
  if (payload.empty()) {
    return std::nullopt;
  }

  DaemonStatusQueryPayload parsed;
  std::size_t start = 0U;
  while (start <= payload.size()) {
    const std::size_t end = payload.find(';', start);
    const std::string_view item = end == std::string_view::npos
                                      ? payload.substr(start)
                                      : payload.substr(start, end - start);
    if (!item.empty()) {
      const std::size_t eq = item.find('=');
      if (eq == std::string_view::npos || eq == 0U || eq + 1U > item.size()) {
        return std::nullopt;
      }

      const std::string key(item.substr(0U, eq));
      const std::string value(item.substr(eq + 1U));
      if (key == "receipt_ref") {
        parsed.receipt_ref = value;
      } else if (key == "actor_ref") {
        parsed.actor_ref = value;
      } else if (key == "ownership_token") {
        parsed.ownership_token = value;
      }
    }

    if (end == std::string_view::npos) {
      break;
    }
    start = end + 1U;
  }

  if (parsed.receipt_ref.empty() || parsed.ownership_token.empty()) {
    return std::nullopt;
  }

  return parsed;
}

struct DaemonDiagPayload {
  std::string command_name;
};

[[nodiscard]] std::optional<DaemonDiagPayload> parse_diag_payload(
    std::string_view payload) {
  if (payload.empty()) {
    return std::nullopt;
  }

  constexpr std::string_view kPrefix = "command_name=";
  if (!payload.starts_with(kPrefix)) {
    return std::nullopt;
  }

  DaemonDiagPayload parsed;
  parsed.command_name = std::string(payload.substr(kPrefix.size()));
  if (parsed.command_name.empty()) {
    return std::nullopt;
  }

  return parsed;
}

struct DaemonKnowledgePayload {
  std::string operation;
  std::string query_text;
};

[[nodiscard]] std::optional<unsigned char> decode_hex_digit(const char digit) {
  if (digit >= '0' && digit <= '9') {
    return static_cast<unsigned char>(digit - '0');
  }
  if (digit >= 'a' && digit <= 'f') {
    return static_cast<unsigned char>(digit - 'a' + 10);
  }
  if (digit >= 'A' && digit <= 'F') {
    return static_cast<unsigned char>(digit - 'A' + 10);
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<std::string> percent_decode_payload_value(
    std::string_view value) {
  std::string decoded;
  decoded.reserve(value.size());
  for (std::size_t index = 0U; index < value.size(); ++index) {
    const char character = value[index];
    if (character != '%') {
      decoded.push_back(character);
      continue;
    }
    if (index + 2U >= value.size()) {
      return std::nullopt;
    }
    const auto high = decode_hex_digit(value[index + 1U]);
    const auto low = decode_hex_digit(value[index + 2U]);
    if (!high.has_value() || !low.has_value()) {
      return std::nullopt;
    }
    decoded.push_back(static_cast<char>((*high << 4U) | *low));
    index += 2U;
  }
  return decoded;
}

[[nodiscard]] std::optional<DaemonKnowledgePayload> parse_knowledge_payload(
    std::string_view payload) {
  if (payload.empty()) {
    return std::nullopt;
  }

  DaemonKnowledgePayload parsed;
  std::size_t start = 0U;
  while (start <= payload.size()) {
    const std::size_t end = payload.find(';', start);
    const std::string_view item = end == std::string_view::npos
                                      ? payload.substr(start)
                                      : payload.substr(start, end - start);
    if (!item.empty()) {
      const std::size_t eq = item.find('=');
      if (eq == std::string_view::npos || eq == 0U || eq + 1U > item.size()) {
        return std::nullopt;
      }

      const std::string key(item.substr(0U, eq));
      const auto decoded_value = percent_decode_payload_value(item.substr(eq + 1U));
      if (!decoded_value.has_value()) {
        return std::nullopt;
      }
      if (key == "operation") {
        parsed.operation = *decoded_value;
      } else if (key == "query_text") {
        parsed.query_text = *decoded_value;
      }
    }

    if (end == std::string_view::npos) {
      break;
    }
    start = end + 1U;
  }

  if (parsed.operation != "health" && parsed.operation != "retrieve" &&
      parsed.operation != "refresh") {
    return std::nullopt;
  }
  if (parsed.operation == "retrieve" && parsed.query_text.empty()) {
    return std::nullopt;
  }
  if (parsed.operation != "retrieve" && !parsed.query_text.empty()) {
    return std::nullopt;
  }

  return parsed;
}

[[nodiscard]] std::string escape_json_string(std::string_view input) {
  std::string output;
  output.reserve(input.size() + 8U);
  for (const unsigned char current : input) {
    switch (current) {
      case '"':
        output += "\\\"";
        break;
      case '\\':
        output += "\\\\";
        break;
      case '\b':
        output += "\\b";
        break;
      case '\f':
        output += "\\f";
        break;
      case '\n':
        output += "\\n";
        break;
      case '\r':
        output += "\\r";
        break;
      case '\t':
        output += "\\t";
        break;
      default:
        if (current < 0x20U) {
          constexpr char kHex[] = "0123456789abcdef";
          output += "\\u00";
          output.push_back(kHex[(current >> 4U) & 0x0FU]);
          output.push_back(kHex[current & 0x0FU]);
        } else {
          output.push_back(static_cast<char>(current));
        }
        break;
    }
  }
  return output;
}

[[nodiscard]] std::string json_string(std::string_view input) {
  return std::string("\"") + escape_json_string(input) + "\"";
}

[[nodiscard]] std::string json_array(const std::vector<std::string>& values) {
  std::string output = "[";
  for (std::size_t index = 0U; index < values.size(); ++index) {
    if (index > 0U) {
      output += ',';
    }
    output += json_string(values[index]);
  }
  output += ']';
  return output;
}

[[nodiscard]] std::string bool_json(const bool value) {
  return value ? "true" : "false";
}

[[nodiscard]] std::string health_state_name(const knowledge::HealthState state) {
  switch (state) {
    case knowledge::HealthState::Unknown:
      return "unknown";
    case knowledge::HealthState::Healthy:
      return "healthy";
    case knowledge::HealthState::Degraded:
      return "degraded";
    case knowledge::HealthState::Unhealthy:
      return "unhealthy";
  }
  return "unknown";
}

[[nodiscard]] std::string freshness_state_name(const knowledge::FreshnessState state) {
  switch (state) {
    case knowledge::FreshnessState::Fresh:
      return "fresh";
    case knowledge::FreshnessState::StaleAllowed:
      return "stale_allowed";
    case knowledge::FreshnessState::StaleRejected:
      return "stale_rejected";
    case knowledge::FreshnessState::Unknown:
      return "unknown";
  }
  return "unknown";
}

[[nodiscard]] std::string refresh_status_name(const knowledge::RefreshStatus status) {
  switch (status) {
    case knowledge::RefreshStatus::Accepted:
      return "accepted";
    case knowledge::RefreshStatus::Busy:
      return "busy";
    case knowledge::RefreshStatus::Failed:
      return "failed";
  }
  return "failed";
}

[[nodiscard]] std::string retrieval_mode_name(const knowledge::RetrievalMode mode) {
  switch (mode) {
    case knowledge::RetrievalMode::LexicalOnly:
      return "lexical_only";
    case knowledge::RetrievalMode::DenseOnly:
      return "dense_only";
    case knowledge::RetrievalMode::Hybrid:
      return "hybrid";
  }
  return "lexical_only";
}

[[nodiscard]] RuntimeDispatchResult make_knowledge_response(
    const InboundPacket& packet,
    std::string payload) {
  RuntimeDispatchResult result;
  result.disposition = AccessDisposition::Completed;

  PublishEnvelope envelope;
  envelope.request_id = packet.packet_id;
  envelope.result_id = packet.packet_id;
  envelope.protocol_kind = packet.protocol_kind;
  envelope.protocol_status_hint = "200";
  envelope.payload = payload;
  dasall::contracts::AgentResult agent_result;
  agent_result.request_id = packet.packet_id;
  agent_result.response_text = std::move(payload);
  agent_result.task_completed = true;
  envelope.agent_result = std::move(agent_result);
  result.publish_envelope = std::move(envelope);
  return result;
}

[[nodiscard]] std::string format_knowledge_health_payload(
    const knowledge::KnowledgeHealthSnapshot& snapshot) {
  std::string payload = "{\"operation\":\"health\"";
  payload += ",\"state\":" + json_string(health_state_name(snapshot.state));
  payload += ",\"active_snapshot_id\":" + json_string(snapshot.active_snapshot_id);
  payload += ",\"freshness_state\":" + json_string(freshness_state_name(snapshot.freshness_state));
  payload += ",\"vector_backend_available\":" + bool_json(snapshot.vector_backend_available);
  payload += ",\"last_known_good_available\":" + bool_json(snapshot.last_known_good_available);
  payload += ",\"degraded_return_count\":" + std::to_string(snapshot.degraded_return_count);
  payload += ",\"reason_codes\":" + json_array(snapshot.reason_codes);
  payload += '}';
  return payload;
}

[[nodiscard]] std::string format_knowledge_refresh_payload(
    const knowledge::RefreshResult& refresh_result) {
  std::string payload = "{\"operation\":\"refresh\"";
  payload += ",\"status\":" + json_string(refresh_status_name(refresh_result.status));
  payload += ",\"refresh_id\":" + json_string(refresh_result.refresh_id);
  if (refresh_result.error.has_value()) {
    payload += ",\"error_ref\":" +
               json_string(refresh_result.error->source_ref.ref_id);
  }
  payload += '}';
  return payload;
}

[[nodiscard]] std::string format_knowledge_retrieve_payload(
    const knowledge::KnowledgeRetrieveResult& retrieve_result) {
  std::size_t slice_count = 0U;
  std::string first_citation;
  std::string first_snippet;
  if (retrieve_result.evidence.has_value()) {
    slice_count = retrieve_result.evidence->slices.size();
    if (!retrieve_result.evidence->slices.empty()) {
      first_citation = retrieve_result.evidence->slices.front().citation_ref;
      first_snippet = retrieve_result.evidence->slices.front().snippet;
    }
  }

  std::string payload = "{\"operation\":\"retrieve\"";
  payload += ",\"ok\":" + bool_json(retrieve_result.ok);
  payload += ",\"mode\":" + json_string(retrieval_mode_name(retrieve_result.mode));
  payload += ",\"slice_count\":" + std::to_string(slice_count);
  payload += ",\"first_citation\":" + json_string(first_citation);
  payload += ",\"first_snippet\":" + json_string(first_snippet);
  if (retrieve_result.error.has_value()) {
    payload += ",\"error_ref\":" +
               json_string(retrieve_result.error->source_ref.ref_id);
  }
  payload += '}';
  return payload;
}

[[nodiscard]] RuntimeDispatchResult make_knowledge_dispatch_result(
    const InboundPacket& packet,
    const DaemonKnowledgePayload& payload,
    const std::shared_ptr<knowledge::IKnowledgeService>& knowledge_service) {
  if (knowledge_service == nullptr) {
    return make_rejected_result(AccessErrorCode::RuntimeDispatchFailed,
                                "knowledge_unavailable");
  }

  if (payload.operation == "health") {
    return make_knowledge_response(
        packet,
        format_knowledge_health_payload(knowledge_service->health_snapshot()));
  }

  if (payload.operation == "refresh") {
    const auto refresh_result = knowledge_service->request_refresh(knowledge::CorpusChangeSet{});
    if (refresh_result.status == knowledge::RefreshStatus::Accepted) {
      return make_knowledge_response(packet,
                                     format_knowledge_refresh_payload(refresh_result));
    }
    const auto failure_ref = refresh_result.error.has_value() &&
                                     !refresh_result.error->source_ref.ref_id.empty()
                                 ? refresh_result.error->source_ref.ref_id
                                 : std::string("knowledge_refresh_failed");
    return make_rejected_result(AccessErrorCode::RuntimeDispatchFailed,
                                refresh_result.status == knowledge::RefreshStatus::Busy
                                    ? "knowledge_refresh_busy"
                                    : failure_ref);
  }

  knowledge::KnowledgeQuery query;
  query.request_id = packet.packet_id.empty() ? "knowledge-retrieve" : packet.packet_id;
  query.query_text = payload.query_text;
  query.query_kind = knowledge::KnowledgeQueryKind::FactLookup;
  query.top_k = 3U;
  query.max_context_projection_items = 3U;
  query.allow_stale = false;
  const auto retrieve_result = knowledge_service->retrieve(query);
  if (retrieve_result.ok) {
    return make_knowledge_response(packet,
                                   format_knowledge_retrieve_payload(retrieve_result));
  }
  return make_rejected_result(AccessErrorCode::RuntimeDispatchFailed,
                              "knowledge_retrieve_failed");
}

[[nodiscard]] RuntimeDispatchResult make_status_dispatch_result(
    const daemon::DaemonTaskQueryResult& query_result,
    std::string_view request_id) {
  RuntimeDispatchResult result;
  PublishEnvelope envelope;
  envelope.request_id = std::string(request_id);
  envelope.result_id = query_result.receipt_ref;
  envelope.protocol_kind = "ipc_uds";
  envelope.payload = query_result.task_status;

  using QueryStatus = daemon::DaemonTaskQueryStatus;
  switch (query_result.status) {
    case QueryStatus::Active:
    case QueryStatus::Completed:
    case QueryStatus::Cancelled:
      result.disposition = AccessDisposition::Completed;
      envelope.protocol_status_hint = "200";
      {
        dasall::contracts::AgentResult agent_result;
        agent_result.request_id = std::string(request_id);
        agent_result.response_text = query_result.task_status;
        agent_result.task_completed =
            query_result.status != QueryStatus::Active;
        envelope.agent_result = std::move(agent_result);
      }
      break;
    case QueryStatus::Missing:
      result.disposition = AccessDisposition::Rejected;
      result.error_ref = "status_missing";
      envelope.payload = *result.error_ref;
      envelope.protocol_status_hint = "404";
      break;
    case QueryStatus::Expired:
      result.disposition = AccessDisposition::Rejected;
      result.error_ref = "status_expired";
      envelope.payload = *result.error_ref;
      envelope.protocol_status_hint = "410";
      break;
    case QueryStatus::OwnerMismatch:
      result.disposition = AccessDisposition::Rejected;
      result.error_ref = "status_owner_mismatch";
      envelope.payload = *result.error_ref;
      envelope.protocol_status_hint = "403";
      break;
    case QueryStatus::CancelForwardFailed:
      result.disposition = AccessDisposition::Rejected;
      result.error_ref = "status_cancel_forward_failed";
      envelope.payload = *result.error_ref;
      envelope.protocol_status_hint = "503";
      break;
  }

  result.publish_envelope = std::move(envelope);
  return result;
}

[[nodiscard]] RuntimeDispatchResult make_cancel_dispatch_result(
    const daemon::DaemonTaskQueryResult& query_result,
    std::string_view request_id) {
  RuntimeDispatchResult result;
  PublishEnvelope envelope;
  envelope.request_id = std::string(request_id);
  envelope.result_id = query_result.receipt_ref;
  envelope.protocol_kind = "ipc_uds";
  envelope.payload = query_result.task_status;

  using QueryStatus = daemon::DaemonTaskQueryStatus;
  switch (query_result.status) {
    case QueryStatus::Cancelled:
      result.disposition = AccessDisposition::Completed;
      envelope.protocol_status_hint = "200";
      {
        dasall::contracts::AgentResult agent_result;
        agent_result.request_id = std::string(request_id);
        agent_result.response_text = query_result.task_status;
        agent_result.task_completed = true;
        envelope.agent_result = std::move(agent_result);
      }
      break;
    case QueryStatus::Missing:
      result.disposition = AccessDisposition::Rejected;
      result.error_ref = "cancel_missing";
      envelope.payload = *result.error_ref;
      envelope.protocol_status_hint = "404";
      break;
    case QueryStatus::Expired:
      result.disposition = AccessDisposition::Rejected;
      result.error_ref = "cancel_expired";
      envelope.payload = *result.error_ref;
      envelope.protocol_status_hint = "410";
      break;
    case QueryStatus::OwnerMismatch:
      result.disposition = AccessDisposition::Rejected;
      result.error_ref = "cancel_owner_mismatch";
      envelope.payload = *result.error_ref;
      envelope.protocol_status_hint = "403";
      break;
    case QueryStatus::CancelForwardFailed:
      result.disposition = AccessDisposition::Rejected;
      result.error_ref = "cancel_forward_failed";
      envelope.payload = *result.error_ref;
      envelope.protocol_status_hint = "503";
      break;
    case QueryStatus::Active:
    case QueryStatus::Completed:
      result.disposition = AccessDisposition::Rejected;
      result.error_ref = "cancel_unexpected_state";
      envelope.payload = *result.error_ref;
      envelope.protocol_status_hint = "409";
      break;
  }

  result.publish_envelope = std::move(envelope);
  return result;
}

[[nodiscard]] RuntimeDispatchResult make_ping_dispatch_result(
    const daemon::DaemonHealthSnapshot& snapshot,
    const InboundPacket& packet) {
  RuntimeDispatchResult result;
  result.disposition = AccessDisposition::Completed;

  PublishEnvelope envelope;
  envelope.request_id = packet.packet_id;
  envelope.result_id = packet.packet_id;
  envelope.protocol_kind = packet.protocol_kind;
  envelope.protocol_status_hint = "200";
  envelope.payload =
      "{\"daemon_version\":\"" + snapshot.ping.daemon_version +
      "\",\"schema_version\":\"" + snapshot.ping.schema_version +
      "\",\"profile_id\":\"" + snapshot.ping.profile_id +
      "\",\"request_id\":\"" + snapshot.ping.request_id +
      "\",\"readiness\":\"" + snapshot.ping.readiness_summary + "\"}";
  dasall::contracts::AgentResult ping_response;
  ping_response.request_id = packet.packet_id;
  ping_response.response_text = envelope.payload;
  ping_response.task_completed = true;
  envelope.agent_result = std::move(ping_response);
  result.publish_envelope = std::move(envelope);
  return result;
}

[[nodiscard]] RuntimeDispatchResult make_readiness_dispatch_result(
    const daemon::DaemonHealthSnapshot& snapshot,
    const InboundPacket& packet) {
  RuntimeDispatchResult result;
  PublishEnvelope envelope;
  envelope.request_id = packet.packet_id;
  envelope.result_id = packet.packet_id;
  envelope.protocol_kind = packet.protocol_kind;

  if (snapshot.readiness.state == daemon::DaemonReadinessState::Ready ||
      snapshot.readiness.state == daemon::DaemonReadinessState::Degraded) {
    result.disposition = AccessDisposition::Completed;
    envelope.protocol_status_hint = "200";
  } else {
    result.disposition = AccessDisposition::Rejected;
    result.error_ref = "daemon_not_ready";
    envelope.protocol_status_hint = "503";
  }

  std::string reasons_payload;
  for (std::size_t index = 0; index < snapshot.readiness.degraded_reasons.size();
       ++index) {
    if (index > 0U) {
      reasons_payload += ",";
    }
    reasons_payload += snapshot.readiness.degraded_reasons[index];
  }

  envelope.payload = "{\"state\":\"" + snapshot.ping.readiness_summary +
                    "\",\"lifecycle_ready\":" +
                    (snapshot.readiness.lifecycle_ready ? "true" : "false") +
                    ",\"listener_ready\":" +
                    (snapshot.readiness.listener_ready ? "true" : "false") +
                    ",\"gateway_ready\":" +
                    (snapshot.readiness.gateway_ready ? "true" : "false") +
                    ",\"bridge_reachable\":" +
                    (snapshot.readiness.bridge_reachable ? "true" : "false") +
                    ",\"runtime_readiness\":\"" +
                    snapshot.readiness.runtime_readiness_label + "\"" +
                    ",\"degraded_reasons\":\"" + reasons_payload + "\"}";
  dasall::contracts::AgentResult readiness_response;
  readiness_response.request_id = packet.packet_id;
  readiness_response.response_text = envelope.payload;
  readiness_response.task_completed = true;
  envelope.agent_result = std::move(readiness_response);
  result.publish_envelope = std::move(envelope);
  return result;
}

[[nodiscard]] std::shared_ptr<AccessGateway::SubmitPipeline>
build_daemon_submit_pipeline(
  const DaemonAccessPipelineOptions& options,
  std::shared_ptr<AccessObservabilityBridge> observability_bridge) {
  auto resolved_options = options;
  if (!resolved_options.runtime_dispatch_backend ||
    !project_access_views_from_runtime_policy(resolved_options)) {
  return nullptr;
  }

  auto request_validator =
    std::make_shared<RequestValidator>(resolved_options.publish_view,
                     resolved_options.bootstrap_config.allowed_protocols);
  auto subject_resolver = std::make_shared<SubjectResolver>();
  auto authenticator_chain = std::make_shared<AuthenticatorChain>();
  auto policy_gate = std::make_shared<AccessPolicyGate>();
  auto admission_controller =
    std::make_shared<AdmissionController>(resolved_options.admission_view);
  auto request_normalizer =
    std::make_shared<RequestNormalizer>(resolved_options.bootstrap_config,
                      resolved_options.publish_view);
  auto runtime_bridge = std::make_shared<RuntimeBridge>(
    resolved_options.runtime_dispatch_backend,
    resolved_options.runtime_cancel_backend);
  auto result_publisher =
    std::make_shared<ResultPublisher>(resolved_options.publish_backend);
  auto async_task_registry = resolve_async_task_registry(resolved_options);
  auto task_query_handler = std::make_shared<daemon::DaemonTaskQueryHandler>(
      *async_task_registry);
  auto health_service = std::make_shared<daemon::DaemonHealthService>(
      resolved_options.daemon_version,
      std::string(daemon::kDaemonProtocolSchemaVersion),
      resolved_options.daemon_profile_id);
  auto diagnostics_handler = std::make_shared<daemon::DaemonDiagnosticsHandler>(
      resolved_options.diagnostics_service,
      resolved_options.daemon_diagnostics_enabled,
      resolved_options.daemon_diagnostics_enabled_state);

  auto submit_pipeline =
      std::make_shared<AccessGateway::SubmitPipeline>(
          [request_validator,
           subject_resolver,
           authenticator_chain,
           policy_gate,
           admission_controller,
           request_normalizer,
           runtime_bridge,
           result_publisher,
           observability_bridge,
           async_task_registry,
           task_query_handler,
           health_service,
           diagnostics_handler,
           resolved_options](const InboundPacket& packet) -> RuntimeDispatchResult {
            const bool diagnostics_enabled =
              resolved_options.daemon_diagnostics_enabled_state
                ? resolved_options.daemon_diagnostics_enabled_state->load()
                : resolved_options.daemon_diagnostics_enabled;

            (void)observability_bridge->emit_daemon_request_fact(
                packet,
                packet.packet_id,
                std::string_view{},
                std::string_view{},
                "READY",
                packet.peer_ref);

            if (packet.packet_id == "ping") {
              daemon::DaemonHealthInput input;
              input.lifecycle_ready = true;
                input.listener_ready = resolved_options.daemon_listener_ready;
                input.gateway_ready = resolved_options.daemon_gateway_ready;
              input.bridge_reachable =
                  resolved_options.daemon_bridge_reachable &&
                  static_cast<bool>(resolved_options.runtime_dispatch_backend);
              input.diagnostics_enabled = diagnostics_enabled;
              input.runtime_readiness_label =
                  resolved_options.daemon_runtime_readiness_label;
              if (input.runtime_readiness_label == "stub-ready") {
                input.bridge_reachable = false;
                input.degraded_reasons.push_back("runtime_entrypoint_stub_ready");
              } else if (input.runtime_readiness_label == "degraded-ready") {
                input.degraded_reasons.push_back("runtime_entrypoint_degraded_ready");
              }
              if (!input.bridge_reachable) {
                input.degraded_reasons.push_back("runtime_bridge_unreachable");
              }
              const auto snapshot = health_service->snapshot(input, packet.packet_id);
              return make_ping_dispatch_result(snapshot, packet);
            }

            if (packet.packet_id == "readiness") {
              daemon::DaemonHealthInput input;
              input.lifecycle_ready = true;
                input.listener_ready = resolved_options.daemon_listener_ready;
                input.gateway_ready = resolved_options.daemon_gateway_ready;
              input.bridge_reachable =
                  resolved_options.daemon_bridge_reachable &&
                  static_cast<bool>(resolved_options.runtime_dispatch_backend);
              input.diagnostics_enabled = diagnostics_enabled;
              input.runtime_readiness_label =
                  resolved_options.daemon_runtime_readiness_label;
              if (input.runtime_readiness_label == "stub-ready") {
                input.bridge_reachable = false;
                input.degraded_reasons.push_back("runtime_entrypoint_stub_ready");
              } else if (input.runtime_readiness_label == "degraded-ready") {
                input.degraded_reasons.push_back("runtime_entrypoint_degraded_ready");
              }
              if (!input.bridge_reachable) {
                input.degraded_reasons.push_back("runtime_bridge_unreachable");
              }
              const auto snapshot = health_service->snapshot(input, packet.packet_id);
              return make_readiness_dispatch_result(snapshot, packet);
            }

            if (packet.packet_id == "status") {
              const auto query_payload = parse_status_query_payload(packet.payload);
              if (!query_payload.has_value()) {
                return make_rejected_result(AccessErrorCode::ValidationRejected,
                                            "status_query_payload_invalid");
              }

              const daemon::DaemonTaskOwner owner{
                  .actor_ref = query_payload->actor_ref,
                  .ownership_token = query_payload->ownership_token,
              };
              const auto query_result = task_query_handler->handle_status(
                  query_payload->receipt_ref,
                  owner);
              return make_status_dispatch_result(query_result, packet.packet_id);
            }

            if (packet.packet_id == "unknown" || packet.packet_id == "unknown_command") {
              return make_rejected_result(AccessErrorCode::ValidationRejected,
                                          "unknown_command");
            }

            PeerMetadata peer_metadata;
            peer_metadata.local_peer = parse_local_peer_fact(packet);

            ResolverView resolver_view;
            resolver_view.trusted_local_subjects =
              resolved_options.auth_view.trusted_local_subjects;
            resolver_view.strict_auth_required =
              resolved_options.auth_view.strict_auth_required;
            resolver_view.allow_remote_challenge = false;

            const auto resolved_subject =
                subject_resolver->resolve(packet, peer_metadata, resolver_view);
            const auto auth_outcome =
              authenticator_chain->authenticate(resolved_subject,
                               resolved_options.auth_view);
            if (!auth_outcome.authenticated) {
              if (auth_outcome.failure_reason.has_value() &&
                  *auth_outcome.failure_reason == "authentication_failed") {
                (void)observability_bridge->emit_peer_identity_denied(
                    packet.packet_id,
                    std::string_view{},
                    "NOT_READY",
                    packet.peer_ref);
              }
              if (auth_outcome.requires_challenge()) {
                return make_rejected_result(AccessErrorCode::AuthenticationChallengeRequired,
                                            auth_outcome.challenge->reason_code);
              }
              return make_rejected_result(AccessErrorCode::AuthenticationFailed,
                                          auth_outcome.failure_reason.value_or(
                                              std::string("authentication_failed")));
            }

            AccessPolicyEvaluationInput policy_input;
            policy_input.authentication = auth_outcome;
            policy_input.packet = packet;

            PolicyBackendSnapshot policy_backend;
            policy_backend.backend_available =
              resolved_options.policy_backend_available;
            policy_backend.allow_submit = resolved_options.allow_submit;

            const auto policy_result =
                policy_gate->evaluate_submit(policy_input, policy_backend);
            if (policy_result.requires_confirmation) {
              return make_rejected_result(AccessErrorCode::ConfirmationRequired,
                                          policy_result.decision_proof.reason_code);
            }
            if (!policy_result.allowed) {
              const auto policy_request =
                build_policy_denied_observability_request(
                  packet,
                  auth_outcome,
                  policy_result);
              (void)observability_bridge->emit_policy_denied(
                policy_request,
                policy_result.reject_reason.value_or(
                  std::string("authorization_denied")));
              return make_rejected_result(AccessErrorCode::AuthorizationDenied,
                                          policy_result.reject_reason.value_or(
                                              std::string("authorization_denied")));
            }

            if (packet.packet_id == "knowledge") {
              const auto knowledge_payload = parse_knowledge_payload(packet.payload);
              if (!knowledge_payload.has_value()) {
                return make_rejected_result(AccessErrorCode::ValidationRejected,
                                            "knowledge_payload_invalid");
              }

              return make_knowledge_dispatch_result(packet,
                                                    *knowledge_payload,
                                                    resolved_options.knowledge_service);
            }

            if (packet.packet_id == "diag" || packet.packet_id == "diagnostics") {
              const auto diag_payload = parse_diag_payload(packet.payload);
              if (!diag_payload.has_value()) {
                return make_rejected_result(AccessErrorCode::ValidationRejected,
                                            "diag_command_invalid");
              }

              PolicyBackendSnapshot diag_backend = policy_backend;
              diag_backend.allow_submit = false;
                diag_backend.allow_diagnostics = true;
              const auto diag_policy_result = policy_gate->evaluate_diagnostics_request(
                  policy_input,
                  diag_payload->command_name,
                  diag_backend);
              if (!diag_policy_result.allowed) {
                return make_rejected_result(
                    AccessErrorCode::AuthorizationDenied,
                    diag_policy_result.reject_reason.value_or(
                        std::string("diag_command_denied")));
              }

              return diagnostics_handler->handle_diag(
                  diag_payload->command_name,
                  packet.packet_id,
                  auth_outcome.subject_identity.actor_ref);
            }

            if (packet.packet_id == "cancel") {
              const auto cancel_payload = parse_status_query_payload(packet.payload);
              if (!cancel_payload.has_value()) {
                return make_rejected_result(AccessErrorCode::ValidationRejected,
                                            "cancel_query_payload_invalid");
              }

              const daemon::DaemonTaskOwner owner{
                  .actor_ref = auth_outcome.subject_identity.actor_ref,
                  .ownership_token = cancel_payload->ownership_token,
              };
              const auto cancel_result = task_query_handler->handle_cancel(
                  cancel_payload->receipt_ref,
                  owner,
                  [runtime_bridge](std::string_view request_id,
                                   std::string_view actor_ref) {
                    return runtime_bridge->cancel(request_id, actor_ref);
                  });
              return make_cancel_dispatch_result(cancel_result, packet.packet_id);
            }

            RuntimeDispatchRequest runtime_request;
            runtime_request.packet = packet;
            runtime_request.subject_identity = auth_outcome.subject_identity;
            runtime_request.decision_proof = policy_result.decision_proof;
            runtime_request.async_allowed = packet.async_preferred;
            runtime_request.stream_requested = packet.stream_requested;
            runtime_request.request_context["request_id"] = packet.packet_id;
            if (packet.trace_id.has_value() && !packet.trace_id->empty()) {
              runtime_request.request_context["trace_id"] = *packet.trace_id;
            }
            if (packet.session_hint.has_value() && !packet.session_hint->empty()) {
              runtime_request.request_context["session_id"] = *packet.session_hint;
            }
            project_packet_headers_to_request_context(packet, runtime_request);
            if (!runtime_request.request_context.contains("idempotency_key")) {
              runtime_request.request_context["idempotency_key"] = packet.packet_id;
            }

            const auto validation_result =
              request_validator->validate_packet(runtime_request);
            if (!validation_result.accepted) {
              const AccessError error = validation_result.error.value_or(
                make_access_error(AccessErrorCode::ValidationRejected,
                        "request validator rejected packet"));
              return make_rejected_result(error.code, error.reason);
            }

            const auto admission_result = admission_controller->admit(runtime_request);
            if (!admission_result.admitted) {
              if (admission_result.replay_hit) {
                return make_replay_result(admission_result.replay_receipt_ref);
              }

              const auto admission_error_code =
                  admission_result.conflict_hit
                      ? AccessErrorCode::IdempotencyConflict
                      : AccessErrorCode::AdmissionRejected;
              return make_rejected_result(admission_error_code,
                                          admission_result.reject_reason.value_or(
                                              std::string("admission_rejected")));
            }

            const auto normalized = request_normalizer->normalize(runtime_request);
            if (!normalized.normalized || normalized.error.has_value()) {
              if (admission_result.ticket_ref.has_value()) {
                admission_controller->release_ticket(*admission_result.ticket_ref);
              }
              const AccessError error = normalized.error.value_or(
                  make_access_error(AccessErrorCode::ValidationRejected,
                                    "request normalizer rejected packet"));
              return make_rejected_result(error.code, error.reason);
            }

            const auto dispatch_started_at = std::chrono::steady_clock::now();
            auto dispatch_result = runtime_bridge->dispatch(normalized.runtime_request);
            const auto dispatch_latency_ms =
              std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - dispatch_started_at)
                .count();
            if (admission_result.ticket_ref.has_value()) {
              admission_controller->record_completion(*admission_result.ticket_ref,
                                                      dispatch_result);
            }

            (void)observability_bridge->emit_dispatch_result(
              normalized.runtime_request,
              dispatch_result,
              dispatch_latency_ms);

            if (!attach_async_receipt(dispatch_result,
                                      normalized.runtime_request,
                                      *async_task_registry,
                                      *observability_bridge)) {
              return make_rejected_result(AccessErrorCode::InternalError,
                                          "ownership_secret_unavailable");
            }

            if (dispatch_result.disposition == AccessDisposition::Rejected &&
                dispatch_result.publish_envelope.has_value() &&
                dispatch_result.publish_envelope->agent_result.has_value()) {
              const auto publish_attempt =
                  result_publisher->publish(normalized.runtime_request,
                                           *dispatch_result.publish_envelope->agent_result);
              if (!publish_attempt.published && publish_attempt.error.has_value()) {
                (void)observability_bridge->emit_publish_failed(
                    publish_attempt.envelope,
                    publish_attempt.error->code,
                    publish_attempt.error->detail.empty()
                        ? publish_attempt.error->reason
                        : publish_attempt.error->detail);
              }
              dispatch_result.publish_envelope = publish_attempt.envelope;
            }

            return dispatch_result;
          });

  return submit_pipeline;
}

[[nodiscard]] std::shared_ptr<AccessGateway::SubmitPipeline>
build_gateway_submit_pipeline(
    const GatewayAccessPipelineOptions& options,
    std::shared_ptr<AccessObservabilityBridge> observability_bridge) {
  auto resolved_options = options;
  if (!resolved_options.runtime_dispatch_backend ||
    !project_access_views_from_runtime_policy(resolved_options)) {
    return nullptr;
  }

  auto request_validator =
    std::make_shared<RequestValidator>(resolved_options.publish_view,
                     resolved_options.bootstrap_config.allowed_protocols);
  auto subject_resolver = std::make_shared<SubjectResolver>();
  auto authenticator_chain = std::make_shared<AuthenticatorChain>();
  auto policy_gate = std::make_shared<AccessPolicyGate>();
  auto admission_controller =
    std::make_shared<AdmissionController>(resolved_options.admission_view);
  auto request_normalizer =
    std::make_shared<RequestNormalizer>(resolved_options.bootstrap_config,
                      resolved_options.publish_view);
  auto runtime_bridge =
    std::make_shared<RuntimeBridge>(resolved_options.runtime_dispatch_backend, nullptr);
  auto result_publisher =
    std::make_shared<ResultPublisher>(resolved_options.publish_backend);
  auto async_task_registry = resolve_async_task_registry(resolved_options);

  auto submit_pipeline = std::make_shared<AccessGateway::SubmitPipeline>(
      [request_validator,
       subject_resolver,
       authenticator_chain,
       policy_gate,
       admission_controller,
       request_normalizer,
       runtime_bridge,
       result_publisher,
       observability_bridge,
      async_task_registry,
      resolved_options](const InboundPacket& packet) -> RuntimeDispatchResult {
        (void)observability_bridge->emit_request_received(
            packet,
            packet.packet_id,
            std::string_view{},
            std::string_view{},
            packet.peer_ref.empty()
                ? std::nullopt
                : std::optional<std::string_view>(packet.peer_ref));

        PeerMetadata peer_metadata = parse_gateway_peer_metadata(packet);

        ResolverView resolver_view;
        resolver_view.trusted_local_subjects =
          resolved_options.auth_view.trusted_local_subjects;
        resolver_view.strict_auth_required =
          resolved_options.auth_view.strict_auth_required;
        resolver_view.allow_remote_challenge = true;

        const auto resolved_subject =
            subject_resolver->resolve(packet, peer_metadata, resolver_view);
        const auto auth_outcome =
          authenticator_chain->authenticate(resolved_subject,
                           resolved_options.auth_view);
        if (!auth_outcome.authenticated) {
          if (auth_outcome.failure_reason.has_value()) {
            (void)observability_bridge->emit_auth_failed(
                packet,
                packet.packet_id,
                std::string_view{},
                *auth_outcome.failure_reason,
                packet.peer_ref.empty()
                    ? std::nullopt
                    : std::optional<std::string_view>(packet.peer_ref));
          }
          if (auth_outcome.requires_challenge()) {
            return make_rejected_result(
                AccessErrorCode::AuthenticationChallengeRequired,
                auth_outcome.challenge->reason_code);
          }
          return make_rejected_result(
              AccessErrorCode::AuthenticationFailed,
              auth_outcome.failure_reason.value_or(
                  std::string("authentication_failed")));
        }

        AccessPolicyEvaluationInput policy_input;
        policy_input.authentication = auth_outcome;
        policy_input.packet = packet;

        PolicyBackendSnapshot policy_backend;
        policy_backend.backend_available = resolved_options.policy_backend_available;
        policy_backend.allow_submit = resolved_options.allow_submit;

        const auto policy_result =
            policy_gate->evaluate_submit(policy_input, policy_backend);
        if (policy_result.requires_confirmation) {
          return make_rejected_result(AccessErrorCode::ConfirmationRequired,
                                      policy_result.decision_proof.reason_code);
        }
        if (!policy_result.allowed) {
          const auto policy_request = build_policy_denied_observability_request(
            packet,
            auth_outcome,
            policy_result);
          (void)observability_bridge->emit_policy_denied(
            policy_request,
            policy_result.reject_reason.value_or(
              std::string("authorization_denied")));
          return make_rejected_result(
              AccessErrorCode::AuthorizationDenied,
              policy_result.reject_reason.value_or(
                  std::string("authorization_denied")));
        }

        RuntimeDispatchRequest runtime_request;
        runtime_request.packet = packet;
        runtime_request.subject_identity = auth_outcome.subject_identity;
        runtime_request.decision_proof = policy_result.decision_proof;
        runtime_request.async_allowed = packet.async_preferred;
        runtime_request.stream_requested = packet.stream_requested;
        runtime_request.request_context["request_id"] = packet.packet_id;
        if (packet.trace_id.has_value() && !packet.trace_id->empty()) {
          runtime_request.request_context["trace_id"] = *packet.trace_id;
        }
        if (packet.session_hint.has_value() && !packet.session_hint->empty()) {
          runtime_request.request_context["session_id"] = *packet.session_hint;
        }
        project_packet_headers_to_request_context(packet, runtime_request);
        if (!runtime_request.request_context.contains("idempotency_key")) {
          runtime_request.request_context["idempotency_key"] = packet.packet_id;
        }

        const auto validation_result = request_validator->validate_packet(runtime_request);
        if (!validation_result.accepted) {
          const AccessError error = validation_result.error.value_or(
              make_access_error(AccessErrorCode::ValidationRejected,
                                "request validator rejected packet"));
          return make_rejected_result(error.code, error.reason);
        }

        const auto admission_result = admission_controller->admit(runtime_request);
        if (!admission_result.admitted) {
          if (admission_result.replay_hit) {
            return make_replay_result(admission_result.replay_receipt_ref);
          }

          const auto admission_error_code =
              admission_result.conflict_hit
                  ? AccessErrorCode::IdempotencyConflict
                  : AccessErrorCode::AdmissionRejected;
          return make_rejected_result(
              admission_error_code,
              admission_result.reject_reason.value_or(
                  std::string("admission_rejected")));
        }

        const auto normalized = request_normalizer->normalize(runtime_request);
        if (!normalized.normalized || normalized.error.has_value()) {
          if (admission_result.ticket_ref.has_value()) {
            admission_controller->release_ticket(*admission_result.ticket_ref);
          }
          const AccessError error = normalized.error.value_or(
              make_access_error(AccessErrorCode::ValidationRejected,
                                "request normalizer rejected packet"));
          return make_rejected_result(error.code, error.reason);
        }

        const auto dispatch_started_at = std::chrono::steady_clock::now();
        auto dispatch_result = runtime_bridge->dispatch(normalized.runtime_request);
        const auto dispatch_latency_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - dispatch_started_at)
                .count();
        if (admission_result.ticket_ref.has_value()) {
          admission_controller->record_completion(*admission_result.ticket_ref,
                                                  dispatch_result);
        }

        (void)observability_bridge->emit_dispatch_result(
            normalized.runtime_request,
            dispatch_result,
            dispatch_latency_ms);

        if (!attach_async_receipt(dispatch_result,
                      normalized.runtime_request,
                      *async_task_registry,
                      *observability_bridge)) {
          return make_rejected_result(AccessErrorCode::InternalError,
                        "ownership_secret_unavailable");
        }

        if (dispatch_result.disposition == AccessDisposition::Rejected &&
            dispatch_result.publish_envelope.has_value() &&
            dispatch_result.publish_envelope->agent_result.has_value()) {
          const auto publish_attempt =
              result_publisher->publish(normalized.runtime_request,
                                        *dispatch_result.publish_envelope->agent_result);
          if (!publish_attempt.published && publish_attempt.error.has_value()) {
            (void)observability_bridge->emit_publish_failed(
                publish_attempt.envelope,
                publish_attempt.error->code,
                publish_attempt.error->detail.empty()
                    ? publish_attempt.error->reason
                    : publish_attempt.error->detail);
          }
          dispatch_result.publish_envelope = publish_attempt.envelope;
        }

        return dispatch_result;
      });

  return submit_pipeline;
}

}  // namespace

std::shared_ptr<IAccessGateway> create_access_gateway(
    AccessGatewayFactoryOptions options) {
  return std::make_shared<AccessGateway>(std::move(options.submit_pipeline),
                                         std::move(options.publish_backend),
                                         std::move(options.shutdown_observer));
}

std::shared_ptr<IAccessGateway> create_daemon_access_gateway(
    DaemonAccessPipelineOptions options) {
  auto observability_bridge =
      make_observability_bridge(options.observability_emit_backend);
  auto pipeline = build_daemon_submit_pipeline(options, observability_bridge);
  AccessGatewayFactoryOptions gateway_options;
  if (pipeline != nullptr) {
    gateway_options.submit_pipeline = *pipeline;
  }
  gateway_options.shutdown_observer =
      [observability_bridge](std::size_t abandoned_requests) {
        (void)observability_bridge->emit_shutdown_abandoned(
            "DRAINING",
            "daemon-access-gateway",
            static_cast<std::uint32_t>(abandoned_requests));
      };
  return create_access_gateway(std::move(gateway_options));
}

std::shared_ptr<IAccessGateway> create_gateway_access_gateway(
    GatewayAccessPipelineOptions options) {
  auto observability_bridge =
      make_observability_bridge(options.observability_emit_backend);
  auto pipeline = build_gateway_submit_pipeline(options, observability_bridge);

  AccessGatewayFactoryOptions gateway_options;
  if (pipeline != nullptr) {
    gateway_options.submit_pipeline = *pipeline;
  }
  return create_access_gateway(std::move(gateway_options));
}

}  // namespace dasall::access