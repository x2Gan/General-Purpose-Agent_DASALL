#include "AccessGatewayFactory.h"

#include <charconv>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "AccessGateway.h"
#include "AccessObservabilityBridge.h"
#include "AccessPolicyGate.h"
#include "AdmissionController.h"
#include "AsyncTaskRegistry.h"
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

namespace dasall::access {

namespace {

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

  if (snapshot.readiness.state == daemon::DaemonReadinessState::Ready) {
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
  auto request_validator =
      std::make_shared<RequestValidator>(options.publish_view,
                                         options.bootstrap_config.allowed_protocols);
  auto subject_resolver = std::make_shared<SubjectResolver>();
  auto authenticator_chain = std::make_shared<AuthenticatorChain>();
  auto policy_gate = std::make_shared<AccessPolicyGate>();
  auto admission_controller =
      std::make_shared<AdmissionController>(options.admission_view);
  auto request_normalizer =
      std::make_shared<RequestNormalizer>(options.bootstrap_config,
                                          options.publish_view);
  auto runtime_bridge = std::make_shared<RuntimeBridge>(
      options.runtime_dispatch_backend,
      options.runtime_cancel_backend);
  auto result_publisher = std::make_shared<ResultPublisher>(options.publish_backend);
  auto async_task_registry = options.async_task_registry
                                 ? options.async_task_registry
                                 : std::make_shared<AsyncTaskRegistry>(
                                       "daemon-access-secret-v1");
  auto receipt_builder = std::make_shared<daemon::DaemonResponseBuilderWithReceipt>(
      async_task_registry);
  auto task_query_handler = std::make_shared<daemon::DaemonTaskQueryHandler>(
      *async_task_registry);
  auto health_service = std::make_shared<daemon::DaemonHealthService>(
      options.daemon_version,
      std::string(daemon::kDaemonProtocolSchemaVersion),
      options.daemon_profile_id);
  auto diagnostics_handler = std::make_shared<daemon::DaemonDiagnosticsHandler>(
      options.diagnostics_service,
      options.daemon_diagnostics_enabled,
      options.daemon_diagnostics_enabled_state);

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
           receipt_builder,
           task_query_handler,
           health_service,
           diagnostics_handler,
           options](const InboundPacket& packet) -> RuntimeDispatchResult {
            const bool diagnostics_enabled =
              options.daemon_diagnostics_enabled_state
                ? options.daemon_diagnostics_enabled_state->load()
                : options.daemon_diagnostics_enabled;

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
              input.listener_ready = options.daemon_listener_ready;
              input.gateway_ready = options.daemon_gateway_ready;
              input.bridge_reachable =
                  options.daemon_bridge_reachable &&
                  static_cast<bool>(options.runtime_dispatch_backend);
              input.diagnostics_enabled = diagnostics_enabled;
              if (!input.bridge_reachable) {
                input.degraded_reasons.push_back("runtime_bridge_unreachable");
              }
              const auto snapshot = health_service->snapshot(input, packet.packet_id);
              return make_ping_dispatch_result(snapshot, packet);
            }

            if (packet.packet_id == "readiness") {
              daemon::DaemonHealthInput input;
              input.lifecycle_ready = true;
              input.listener_ready = options.daemon_listener_ready;
              input.gateway_ready = options.daemon_gateway_ready;
              input.bridge_reachable =
                  options.daemon_bridge_reachable &&
                  static_cast<bool>(options.runtime_dispatch_backend);
              input.diagnostics_enabled = diagnostics_enabled;
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
            resolver_view.trusted_local_subjects = options.auth_view.trusted_local_subjects;
            resolver_view.strict_auth_required = options.auth_view.strict_auth_required;
            resolver_view.allow_remote_challenge = false;

            const auto resolved_subject =
                subject_resolver->resolve(packet, peer_metadata, resolver_view);
            const auto auth_outcome =
                authenticator_chain->authenticate(resolved_subject, options.auth_view);
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
            policy_backend.backend_available = options.policy_backend_available;
            policy_backend.allow_submit = options.allow_submit;

            const auto policy_result =
                policy_gate->evaluate_submit(policy_input, policy_backend);
            if (policy_result.requires_confirmation) {
              return make_rejected_result(AccessErrorCode::ConfirmationRequired,
                                          policy_result.decision_proof.reason_code);
            }
            if (!policy_result.allowed) {
              return make_rejected_result(AccessErrorCode::AuthorizationDenied,
                                          policy_result.reject_reason.value_or(
                                              std::string("authorization_denied")));
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
            runtime_request.request_context["idempotency_key"] = packet.packet_id;

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

            auto dispatch_result = runtime_bridge->dispatch(normalized.runtime_request);
            if (admission_result.ticket_ref.has_value()) {
              admission_controller->record_completion(*admission_result.ticket_ref,
                                                      dispatch_result);
            }

            if (dispatch_result.disposition == AccessDisposition::AcceptedAsync) {
              const auto receipt = receipt_builder->register_and_build_receipt(
                  normalized.runtime_request,
                  dispatch_result);
              if (receipt != nullptr) {
                dispatch_result.receipt_ref = receipt->receipt_id;
                if (!dispatch_result.publish_envelope.has_value()) {
                  dispatch_result.publish_envelope = PublishEnvelope{};
                }
                dispatch_result.publish_envelope->request_id =
                    normalized.runtime_request.packet.packet_id;
                dispatch_result.publish_envelope->result_id = receipt->receipt_id;
                dispatch_result.publish_envelope->protocol_kind =
                    normalized.runtime_request.packet.protocol_kind;
                dispatch_result.publish_envelope->protocol_status_hint = "202";
                dispatch_result.publish_envelope->payload = "accepted_async";
                dispatch_result.publish_envelope->receipt = *receipt;
                (void)observability_bridge->emit_receipt_event(
                    normalized.runtime_request.packet.packet_id,
                    normalized.runtime_request.packet.packet_id,
                    std::string_view{},
                    "READY",
                    receipt->receipt_id);
              }
            }

            if (dispatch_result.disposition == AccessDisposition::Rejected &&
                dispatch_result.publish_envelope.has_value() &&
                dispatch_result.publish_envelope->agent_result.has_value()) {
              const auto publish_attempt =
                  result_publisher->publish(normalized.runtime_request,
                                           *dispatch_result.publish_envelope->agent_result);
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
  auto observability_bridge = std::make_shared<AccessObservabilityBridge>();
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

}  // namespace dasall::access