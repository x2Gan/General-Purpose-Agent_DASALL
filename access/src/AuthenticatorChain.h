#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "AccessTypes.h"
#include "SubjectResolver.h"

namespace dasall::access {

// AuthChallenge 承接 resolver challenge，并保持协议无关。
struct AuthChallenge {
  std::string challenge_type;
  std::string reason_code;
  std::string detail;

  [[nodiscard]] bool has_consistent_values() const;
};

// AuthenticationOutcome 是认证链唯一输出对象。
// 它只表达 authenticated、challenge、rejected 三类显式结果。
struct AuthenticationOutcome {
  bool authenticated = false;
  bool rejected = false;
  std::string chain_ref;
  SubjectIdentity subject_identity;
  std::optional<AuthChallenge> challenge;
  std::optional<std::string> failure_reason;

  [[nodiscard]] bool requires_challenge() const;
};

class AuthenticatorChain {
 public:
  [[nodiscard]] AuthenticationOutcome authenticate(
      const SubjectResolveOutcome& resolved_subject,
      const AccessAuthView& auth_view) const;

 private:
  [[nodiscard]] std::string select_chain(
      const SubjectResolveOutcome& resolved_subject,
      const AccessAuthView& auth_view) const;

  [[nodiscard]] AuthenticationOutcome verify_credentials(
      const SubjectResolveOutcome& resolved_subject,
      const AccessAuthView& auth_view,
      std::string_view chain_ref) const;

  void merge_subject_attributes(SubjectIdentity& subject_identity,
                                const AccessAuthView& auth_view,
                                std::string_view chain_ref) const;

  [[nodiscard]] std::string map_failure_reason(std::string_view reason_code) const;
};

}  // namespace dasall::access
