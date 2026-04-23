#include "SubjectResolver.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dasall::access {
namespace {

[[nodiscard]] bool is_local_entry_type(const std::string_view entry_type) {
  return entry_type == "cli" || entry_type == "daemon";
}

[[nodiscard]] std::string stable_value_or_unknown(const std::string_view value) {
  return value.empty() ? std::string("unknown") : std::string(value);
}

[[nodiscard]] bool is_allowlisted_local_subject(const ResolverView& view,
                                                const std::string_view actor_ref) {
  return std::find(view.trusted_local_subjects.begin(),
                   view.trusted_local_subjects.end(),
                   actor_ref) != view.trusted_local_subjects.end();
}

[[nodiscard]] std::string local_actor_ref_from_fact(const LocalPeerUidFact& local_peer) {
  if (!local_peer.actor_ref.empty()) {
    return local_peer.actor_ref;
  }

  return "local://uid/" + std::to_string(local_peer.peer_uid);
}

struct SubjectHint {
  std::string actor_ref;
  std::string auth_method;
  std::string subject_type;
  std::optional<std::string> auth_metadata;
};

[[nodiscard]] SubjectIdentity make_identity(const SubjectHint& hint,
                                            const PeerMetadata& peer_metadata) {
  SubjectIdentity subject_identity;
  subject_identity.actor_ref = hint.actor_ref;
  subject_identity.subject_type = hint.subject_type;
  subject_identity.auth_method = hint.auth_method;
  subject_identity.trust_level = "authenticated";
  subject_identity.tenant_ref =
      peer_metadata.tenant_ref.value_or(std::string("default"));
  subject_identity.auth_metadata = hint.auth_metadata;
  return subject_identity;
}

[[nodiscard]] std::vector<SubjectHint> collect_subject_hints(
    const PeerMetadata& peer_metadata) {
  std::vector<SubjectHint> hints;

  if (peer_metadata.certificate_subject.has_value() &&
      !peer_metadata.certificate_subject->empty()) {
    hints.push_back(SubjectHint{
        .actor_ref = "mtls://" + *peer_metadata.certificate_subject,
        .auth_method = "mTLS",
        .subject_type = "service",
        .auth_metadata = peer_metadata.certificate_subject,
    });
  }

  if (peer_metadata.jwt_actor_ref.has_value() && !peer_metadata.jwt_actor_ref->empty()) {
    hints.push_back(SubjectHint{
        .actor_ref = *peer_metadata.jwt_actor_ref,
        .auth_method = "JWT",
        .subject_type = "user",
        .auth_metadata = std::nullopt,
    });
  }

  if (peer_metadata.token_actor_ref.has_value() &&
      !peer_metadata.token_actor_ref->empty()) {
    hints.push_back(SubjectHint{
        .actor_ref = *peer_metadata.token_actor_ref,
        .auth_method = "token",
        .subject_type = "user",
        .auth_metadata = std::nullopt,
    });
  }

  if (peer_metadata.simulator_actor_ref.has_value() &&
      !peer_metadata.simulator_actor_ref->empty()) {
    hints.push_back(SubjectHint{
        .actor_ref = *peer_metadata.simulator_actor_ref,
        .auth_method = "simulator_stub",
        .subject_type = "diagnostic_endpoint",
        .auth_metadata = std::nullopt,
    });
  }

  return hints;
}

}  // namespace

bool ChallengePlan::has_consistent_values() const {
  return !challenge_type.empty() && !reason_code.empty();
}

bool SubjectResolveOutcome::requires_challenge() const {
  return !resolved && !rejected && challenge_plan.has_value() &&
         challenge_plan->has_consistent_values();
}

SubjectResolveOutcome SubjectResolver::resolve(const InboundPacket& packet,
                                               const PeerMetadata& peer_metadata,
                                               const ResolverView& view) const {
  SubjectResolveOutcome outcome;
  outcome.channel_ref = derive_channel_ref(packet);

  if (const auto local_subject = derive_local_subject(packet, peer_metadata, view);
      local_subject.has_value()) {
    outcome.resolved = true;
    outcome.subject_identity = *local_subject;
    return outcome;
  }

  const auto subject_hints = collect_subject_hints(peer_metadata);
  std::set<std::string> distinct_actor_refs;
  for (const auto& hint : subject_hints) {
    distinct_actor_refs.insert(hint.actor_ref);
  }

  if (distinct_actor_refs.size() > 1U) {
    outcome.rejected = true;
    outcome.reject_reason = std::string("identity_conflict");
    return outcome;
  }

  if (!subject_hints.empty()) {
    outcome.resolved = true;
    outcome.subject_identity = make_identity(subject_hints.front(), peer_metadata);
    return outcome;
  }

  if (!is_local_entry_type(packet.entry_type) && view.allow_remote_challenge) {
    outcome.challenge_plan =
        build_challenge_plan(packet, std::string_view("missing_identity_hint"));
    return outcome;
  }

  outcome.rejected = true;
  outcome.reject_reason = view.strict_auth_required
                              ? std::optional<std::string>("missing_identity_hint")
                              : std::optional<std::string>("identity_not_resolved");
  return outcome;
}

std::string SubjectResolver::derive_channel_ref(const InboundPacket& packet) const {
  return "channel://" + stable_value_or_unknown(packet.entry_type) + "/" +
         stable_value_or_unknown(packet.protocol_kind);
}

std::optional<SubjectIdentity> SubjectResolver::derive_local_subject(
    const InboundPacket& packet,
    const PeerMetadata& peer_metadata,
    const ResolverView& view) const {
  if (!is_local_entry_type(packet.entry_type)) {
    return std::nullopt;
  }

  if (!peer_metadata.local_peer.is_local_socket_peer ||
      !peer_metadata.local_peer.eligible_for_local_trusted) {
    return std::nullopt;
  }

  const std::string actor_ref = local_actor_ref_from_fact(peer_metadata.local_peer);
  if (view.trusted_local_subjects.empty() ||
      !is_allowlisted_local_subject(view, actor_ref)) {
    return std::nullopt;
  }

  SubjectIdentity subject_identity;
  subject_identity.actor_ref = actor_ref;
  subject_identity.subject_type = "operator";
  subject_identity.auth_method = "local_trusted";
  subject_identity.trust_level = "trusted";
  subject_identity.tenant_ref =
      peer_metadata.tenant_ref.value_or(std::string("local"));
  subject_identity.auth_metadata = std::string("peer_uid=") +
                                   std::to_string(peer_metadata.local_peer.peer_uid);
  return subject_identity;
}

ChallengePlan SubjectResolver::build_challenge_plan(
    const InboundPacket& packet,
    const std::string_view reason_code) const {
  const std::string challenge_type =
      packet.protocol_kind == "http" ? "http_authenticate" : "credential_prompt";

  return ChallengePlan{
      .challenge_type = challenge_type,
      .reason_code = std::string(reason_code),
      .detail = std::string("additional authentication is required for ") +
                stable_value_or_unknown(packet.entry_type),
  };
}

}  // namespace dasall::access
