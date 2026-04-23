#include <exception>
#include <iostream>
#include <string>

#include "SubjectResolver.h"
#include "support/TestAssertions.h"

namespace {

void resolves_local_trusted_subject_when_actor_is_allowlisted() {
  using dasall::access::InboundPacket;
  using dasall::access::PeerMetadata;
  using dasall::access::ResolverView;
  using dasall::access::SubjectResolver;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  SubjectResolver resolver;
  InboundPacket packet;
  packet.entry_type = "cli";
  packet.protocol_kind = "uds";

  PeerMetadata peer_metadata;
  peer_metadata.local_peer.actor_ref = "local://uid/1000";
  peer_metadata.local_peer.peer_uid = 1000;
  peer_metadata.local_peer.is_local_socket_peer = true;
  peer_metadata.local_peer.eligible_for_local_trusted = true;

  ResolverView view;
  view.trusted_local_subjects.push_back("local://uid/1000");

  const auto outcome = resolver.resolve(packet, peer_metadata, view);
  assert_true(outcome.resolved,
              "allowlisted local peer should resolve as a trusted local subject");
  assert_equal(std::string("local_trusted"), outcome.subject_identity.auth_method,
               "local trusted resolution should set local_trusted auth_method");
  assert_equal(std::string("trusted"), outcome.subject_identity.trust_level,
               "local trusted resolution should mark the subject as trusted");
}

void rejects_local_peer_without_allowlist_support() {
  using dasall::access::InboundPacket;
  using dasall::access::PeerMetadata;
  using dasall::access::ResolverView;
  using dasall::access::SubjectResolver;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  SubjectResolver resolver;
  InboundPacket packet;
  packet.entry_type = "daemon";
  packet.protocol_kind = "uds";

  PeerMetadata peer_metadata;
  peer_metadata.local_peer.actor_ref = "local://uid/1001";
  peer_metadata.local_peer.peer_uid = 1001;
  peer_metadata.local_peer.is_local_socket_peer = true;
  peer_metadata.local_peer.eligible_for_local_trusted = true;

  const auto outcome = resolver.resolve(packet, peer_metadata, ResolverView{});
  assert_true(outcome.rejected,
              "local trusted inference without allowlist support should fail closed");
  assert_true(outcome.reject_reason.has_value(),
              "failed local trusted inference should emit a reject reason");
  assert_equal(std::string("missing_identity_hint"), *outcome.reject_reason,
               "missing allowlist support should not silently downgrade to trusted");
}

}  // namespace

int main() {
  try {
    resolves_local_trusted_subject_when_actor_is_allowlisted();
    rejects_local_peer_without_allowlist_support();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
