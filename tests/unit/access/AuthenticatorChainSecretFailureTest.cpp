#include <exception>
#include <iostream>
#include <string>

#include "AuthenticatorChain.h"
#include "support/TestAssertions.h"

namespace {

void rejects_when_secret_backend_is_unavailable() {
  using dasall::access::AccessAuthView;
  using dasall::access::AuthenticationOutcome;
  using dasall::access::AuthenticatorChain;
  using dasall::access::SubjectResolveOutcome;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  AuthenticatorChain chain;
  SubjectResolveOutcome resolved_subject;
  resolved_subject.resolved = true;
  resolved_subject.channel_ref = "channel://gateway/http";
  resolved_subject.subject_identity.actor_ref = "user://tenant-a/alice";
  resolved_subject.subject_identity.subject_type = "user";
  resolved_subject.subject_identity.auth_method = "JWT";

  AccessAuthView auth_view;
  auth_view.auth_provider_ref = std::string("secret://unavailable");

  const AuthenticationOutcome outcome = chain.authenticate(resolved_subject, auth_view);
  assert_true(outcome.rejected,
              "secret backend failure should reject instead of silently bypassing auth");
  assert_true(outcome.failure_reason.has_value(),
              "secret backend failure should emit an explicit reason");
  assert_equal(std::string("secret_backend_unavailable"), *outcome.failure_reason,
               "failure reason should preserve the secret backend outage semantics");
}

}  // namespace

int main() {
  try {
    rejects_when_secret_backend_is_unavailable();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
