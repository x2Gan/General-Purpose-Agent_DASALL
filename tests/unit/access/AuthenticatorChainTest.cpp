#include <exception>
#include <iostream>
#include <string>

#include "AuthenticatorChain.h"
#include "support/TestAssertions.h"

namespace {

void authenticates_trusted_local_subject() {
  using dasall::access::AccessAuthView;
  using dasall::access::AuthenticationOutcome;
  using dasall::access::AuthenticatorChain;
  using dasall::access::SubjectResolveOutcome;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  AuthenticatorChain chain;
  SubjectResolveOutcome resolved_subject;
  resolved_subject.resolved = true;
  resolved_subject.channel_ref = "channel://cli/uds";
  resolved_subject.subject_identity.actor_ref = "local://uid/1000";
  resolved_subject.subject_identity.subject_type = "operator";
  resolved_subject.subject_identity.auth_method = "local_trusted";

  AccessAuthView auth_view;
  auth_view.trusted_local_subjects.push_back("local://uid/1000");

  const AuthenticationOutcome outcome = chain.authenticate(resolved_subject, auth_view);
  assert_true(outcome.authenticated,
              "allowlisted local trusted subject should authenticate successfully");
  assert_equal(std::string("local_trusted"), outcome.chain_ref,
               "authentication chain should select the local_trusted chain");
  assert_equal(std::string("trusted"), outcome.subject_identity.trust_level,
               "local trusted chain should promote trust_level to trusted");
  assert_true(outcome.subject_identity.auth_metadata.has_value(),
              "successful authentication should annotate auth metadata");
}

}  // namespace

int main() {
  try {
    authenticates_trusted_local_subject();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
