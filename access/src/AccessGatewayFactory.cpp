#include "AccessGatewayFactory.h"

#include <charconv>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "AccessGateway.h"
#include "AccessPolicyGate.h"
#include "AdmissionController.h"
#include "AuthenticatorChain.h"
#include "RequestNormalizer.h"
#include "RequestValidator.h"
#include "ResultPublisher.h"
#include "RuntimeBridge.h"
#include "SubjectResolver.h"

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

[[nodiscard]] std::shared_ptr<AccessGateway::SubmitPipeline>
build_daemon_submit_pipeline(const DaemonAccessPipelineOptions& options) {
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
  auto result_publisher = std::make_shared<ResultPublisher>();

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
           options](const InboundPacket& packet) -> RuntimeDispatchResult {
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
                                         std::move(options.publish_backend));
}

std::shared_ptr<IAccessGateway> create_daemon_access_gateway(
    DaemonAccessPipelineOptions options) {
  auto pipeline = build_daemon_submit_pipeline(options);
  AccessGatewayFactoryOptions gateway_options;
  if (pipeline != nullptr) {
    gateway_options.submit_pipeline = *pipeline;
  }
  return create_access_gateway(std::move(gateway_options));
}

}  // namespace dasall::access