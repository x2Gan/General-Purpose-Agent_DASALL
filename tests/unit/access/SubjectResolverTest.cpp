#include <exception>
#include <iostream>
#include <string>

#include "SubjectResolver.h"
#include "support/TestAssertions.h"

namespace {

void resolves_consistent_jwt_identity() {
  using dasall::access::InboundPacket;
  using dasall::access::PeerMetadata;
  using dasall::access::ResolverView;
  using dasall::access::SubjectResolver;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  SubjectResolver resolver;
  InboundPacket packet;
  packet.entry_type = "gateway";
  packet.protocol_kind = "http";

  PeerMetadata peer_metadata;
  peer_metadata.jwt_actor_ref = std::string("user://tenant-a/alice");
  peer_metadata.tenant_ref = std::string("tenant-a");

  const auto outcome = resolver.resolve(packet, peer_metadata, ResolverView{});
  assert_true(outcome.resolved,
              "resolver should resolve a single consistent JWT identity hint");
  assert_equal(std::string("channel://gateway/http"), outcome.channel_ref,
               "resolver should derive a stable channel_ref");
  assert_equal(std::string("user://tenant-a/alice"),
               outcome.subject_identity.actor_ref,
               "resolver should preserve the actor_ref from the JWT hint");
  assert_equal(std::string("JWT"), outcome.subject_identity.auth_method,
               "resolver should tag JWT-derived identity with JWT auth_method");
}

void rejects_conflicting_identity_hints() {
  using dasall::access::InboundPacket;
  using dasall::access::PeerMetadata;
  using dasall::access::ResolverView;
  using dasall::access::SubjectResolver;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  SubjectResolver resolver;
  InboundPacket packet;
  packet.entry_type = "gateway";
  packet.protocol_kind = "http";

  PeerMetadata peer_metadata;
  peer_metadata.jwt_actor_ref = std::string("user://tenant-a/alice");
  peer_metadata.token_actor_ref = std::string("user://tenant-a/bob");

  const auto outcome = resolver.resolve(packet, peer_metadata, ResolverView{});
  assert_true(outcome.rejected,
              "resolver should reject when multiple identity hints disagree");
  assert_true(outcome.reject_reason.has_value(),
              "conflicting identity hints should expose an explicit reject reason");
  assert_equal(std::string("identity_conflict"), *outcome.reject_reason,
               "reject reason should describe the conflict path");
}

}  // namespace

int main() {
  try {
    resolves_consistent_jwt_identity();
    rejects_conflicting_identity_hints();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
