#include <exception>
#include <iostream>
#include <string>

#include "SubjectResolver.h"
#include "support/TestAssertions.h"

namespace {

void requests_remote_challenge_when_identity_is_missing() {
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

  ResolverView view;
  view.allow_remote_challenge = true;

  const auto outcome = resolver.resolve(packet, PeerMetadata{}, view);
  assert_true(outcome.requires_challenge(),
              "remote request without identity should request a challenge");
  assert_true(outcome.challenge_plan.has_value(),
              "challenge path should return a challenge plan");
  assert_equal(std::string("http_authenticate"),
               outcome.challenge_plan->challenge_type,
               "HTTP challenge should use the HTTP authenticate challenge type");
  assert_equal(std::string("missing_identity_hint"),
               outcome.challenge_plan->reason_code,
               "challenge should retain the reason code for later mapping");
}

void rejects_remote_request_when_challenge_is_disabled() {
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

  ResolverView view;
  view.allow_remote_challenge = false;

  const auto outcome = resolver.resolve(packet, PeerMetadata{}, view);
  assert_true(outcome.rejected,
              "remote request without identity should reject when challenge is disabled");
  assert_true(outcome.reject_reason.has_value(),
              "disabled challenge path should emit a reject reason");
  assert_equal(std::string("missing_identity_hint"), *outcome.reject_reason,
               "reject reason should preserve the missing identity failure");
}

}  // namespace

int main() {
  try {
    requests_remote_challenge_when_identity_is_missing();
    rejects_remote_request_when_challenge_is_disabled();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
