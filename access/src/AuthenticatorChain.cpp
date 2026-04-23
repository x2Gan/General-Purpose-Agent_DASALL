#include "AuthenticatorChain.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

namespace dasall::access {
namespace {

[[nodiscard]] bool uses_external_secret_backend(const std::string_view chain_ref) {
  return chain_ref == "jwt" || chain_ref == "token" || chain_ref == "mtls";
}

[[nodiscard]] bool is_allowlisted_local_subject(const AccessAuthView& auth_view,
                                                const std::string_view actor_ref) {
  return std::find(auth_view.trusted_local_subjects.begin(),
                   auth_view.trusted_local_subjects.end(),
                   actor_ref) != auth_view.trusted_local_subjects.end();
}

}  // namespace

bool AuthChallenge::has_consistent_values() const {
  return !challenge_type.empty() && !reason_code.empty();
}

bool AuthenticationOutcome::requires_challenge() const {
  return !authenticated && !rejected && challenge.has_value() &&
         challenge->has_consistent_values();
}

AuthenticationOutcome AuthenticatorChain::authenticate(
    const SubjectResolveOutcome& resolved_subject,
    const AccessAuthView& auth_view) const {
  if (resolved_subject.requires_challenge()) {
    AuthenticationOutcome outcome;
    outcome.chain_ref = "challenge";
    outcome.challenge = AuthChallenge{
        .challenge_type = resolved_subject.challenge_plan->challenge_type,
        .reason_code = resolved_subject.challenge_plan->reason_code,
        .detail = resolved_subject.challenge_plan->detail,
    };
    return outcome;
  }

  if (resolved_subject.rejected) {
    AuthenticationOutcome outcome;
    outcome.rejected = true;
    outcome.chain_ref = "rejected";
    outcome.failure_reason = map_failure_reason(
        resolved_subject.reject_reason.value_or(std::string("authentication_failed")));
    return outcome;
  }

  const std::string chain_ref = select_chain(resolved_subject, auth_view);
  return verify_credentials(resolved_subject, auth_view, chain_ref);
}

std::string AuthenticatorChain::select_chain(
    const SubjectResolveOutcome& resolved_subject,
    const AccessAuthView& auth_view) const {
  static_cast<void>(auth_view);

  if (!resolved_subject.resolved) {
    return "missing_subject";
  }

  if (resolved_subject.subject_identity.auth_method == "local_trusted") {
    return "local_trusted";
  }
  if (resolved_subject.subject_identity.auth_method == "JWT") {
    return "jwt";
  }
  if (resolved_subject.subject_identity.auth_method == "token") {
    return "token";
  }
  if (resolved_subject.subject_identity.auth_method == "mTLS") {
    return "mtls";
  }
  if (resolved_subject.subject_identity.auth_method == "simulator_stub") {
    return "simulator_stub";
  }

  return "unknown";
}

AuthenticationOutcome AuthenticatorChain::verify_credentials(
    const SubjectResolveOutcome& resolved_subject,
    const AccessAuthView& auth_view,
    const std::string_view chain_ref) const {
  AuthenticationOutcome outcome;
  outcome.chain_ref = std::string(chain_ref);

  if (!resolved_subject.resolved) {
    outcome.rejected = true;
    outcome.failure_reason = map_failure_reason("missing_subject");
    return outcome;
  }

  if (resolved_subject.subject_identity.actor_ref.empty() ||
      resolved_subject.subject_identity.auth_method.empty()) {
    outcome.rejected = true;
    outcome.failure_reason = map_failure_reason("credential_missing");
    return outcome;
  }

  if (chain_ref == "local_trusted") {
    if (!is_allowlisted_local_subject(auth_view,
                                      resolved_subject.subject_identity.actor_ref)) {
      outcome.rejected = true;
      outcome.failure_reason = map_failure_reason("local_trusted_not_allowlisted");
      return outcome;
    }
  } else if (uses_external_secret_backend(chain_ref) &&
             auth_view.auth_provider_ref.has_value() &&
             *auth_view.auth_provider_ref == "secret://unavailable") {
    outcome.rejected = true;
    outcome.failure_reason = map_failure_reason("secret_backend_unavailable");
    return outcome;
  } else if (chain_ref == "unknown" || chain_ref == "missing_subject") {
    outcome.rejected = true;
    outcome.failure_reason = map_failure_reason("credential_missing");
    return outcome;
  }

  outcome.authenticated = true;
  outcome.subject_identity = resolved_subject.subject_identity;
  merge_subject_attributes(outcome.subject_identity, auth_view, chain_ref);
  return outcome;
}

void AuthenticatorChain::merge_subject_attributes(SubjectIdentity& subject_identity,
                                                  const AccessAuthView& auth_view,
                                                  const std::string_view chain_ref) const {
  if (subject_identity.trust_level.empty()) {
    subject_identity.trust_level =
        chain_ref == "local_trusted" ? "trusted" : "authenticated";
  }

  if (subject_identity.tenant_ref.empty()) {
    subject_identity.tenant_ref = "default";
  }

  const std::string metadata_value = std::string("chain=") + std::string(chain_ref);
  if (subject_identity.auth_metadata.has_value() &&
      !subject_identity.auth_metadata->empty()) {
    subject_identity.auth_metadata = *subject_identity.auth_metadata + ";" + metadata_value;
  } else {
    subject_identity.auth_metadata = metadata_value;
  }

  if (chain_ref == "local_trusted" &&
      is_allowlisted_local_subject(auth_view, subject_identity.actor_ref)) {
    subject_identity.trust_level = "trusted";
  }
}

std::string AuthenticatorChain::map_failure_reason(const std::string_view reason_code) const {
  if (reason_code == "identity_conflict") {
    return "identity_conflict";
  }
  if (reason_code == "secret_backend_unavailable") {
    return "secret_backend_unavailable";
  }
  if (reason_code == "local_trusted_not_allowlisted") {
    return "authentication_failed";
  }
  if (reason_code == "missing_identity_hint" || reason_code == "missing_subject" ||
      reason_code == "credential_missing") {
    return "authentication_required";
  }

  return "authentication_failed";
}

}  // namespace dasall::access
