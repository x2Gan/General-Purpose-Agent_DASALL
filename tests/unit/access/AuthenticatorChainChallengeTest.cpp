#include <exception>
#include <iostream>
#include <string>

#include "AuthenticatorChain.h"
#include "support/TestAssertions.h"

namespace {

void forwards_resolver_challenge_without_downgrading_to_success() {
  using dasall::access::AccessAuthView;
  using dasall::access::AuthChallenge;
  using dasall::access::AuthenticationOutcome;
  using dasall::access::AuthenticatorChain;
  using dasall::access::ChallengePlan;
  using dasall::access::SubjectResolveOutcome;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  AuthenticatorChain chain;
  SubjectResolveOutcome resolved_subject;
  resolved_subject.channel_ref = "channel://gateway/http";
  resolved_subject.challenge_plan = ChallengePlan{
      .challenge_type = "http_authenticate",
      .reason_code = "missing_identity_hint",
      .detail = "additional authentication is required",
  };

  const AuthenticationOutcome outcome = chain.authenticate(resolved_subject, AccessAuthView{});
  assert_true(outcome.requires_challenge(),
              "authenticator chain should preserve resolver challenge output");
  assert_equal(std::string("challenge"), outcome.chain_ref,
               "challenge path should expose the synthetic challenge chain ref");
  assert_equal(std::string("http_authenticate"), outcome.challenge->challenge_type,
               "challenge type should flow through from resolver");
}

}  // namespace

int main() {
  try {
    forwards_resolver_challenge_without_downgrading_to_success();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
