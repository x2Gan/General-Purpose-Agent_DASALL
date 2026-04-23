#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "AccessTypes.h"

namespace dasall::access {

// PeerMetadata 汇总入口适配器和平台层已经拿到的主体 hints。
// 它只承载事实输入，不表达最终认证是否成功。
struct PeerMetadata {
  std::optional<std::string> certificate_subject;
  std::optional<std::string> jwt_actor_ref;
  std::optional<std::string> token_actor_ref;
  std::optional<std::string> simulator_actor_ref;
  std::optional<std::string> tenant_ref;
  std::optional<std::string> device_ref;
  LocalPeerUidFact local_peer;
};

// ResolverView 是 AccessAuthView 的最小只读投影。
// SubjectResolver 只消费 local trusted allowlist 和 challenge 策略。
struct ResolverView {
  std::vector<std::string> trusted_local_subjects;
  bool strict_auth_required = true;
  bool allow_remote_challenge = true;
};

// ChallengePlan 保持协议无关，后续由认证链或协议层映射成具体交互形式。
struct ChallengePlan {
  std::string challenge_type;
  std::string reason_code;
  std::string detail;

  [[nodiscard]] bool has_consistent_values() const;
};

// SubjectResolveOutcome 是 resolver 对上游唯一暴露的结论对象。
// 三类出口分别是 resolved、challenge 和 rejected，禁止 silent fallback。
struct SubjectResolveOutcome {
  bool resolved = false;
  bool rejected = false;
  std::string channel_ref;
  SubjectIdentity subject_identity;
  std::optional<ChallengePlan> challenge_plan;
  std::optional<std::string> reject_reason;

  [[nodiscard]] bool requires_challenge() const;
};

class SubjectResolver {
 public:
  [[nodiscard]] SubjectResolveOutcome resolve(const InboundPacket& packet,
                                             const PeerMetadata& peer_metadata,
                                             const ResolverView& view) const;

 private:
  [[nodiscard]] std::string derive_channel_ref(const InboundPacket& packet) const;

  [[nodiscard]] std::optional<SubjectIdentity> derive_local_subject(
      const InboundPacket& packet,
      const PeerMetadata& peer_metadata,
      const ResolverView& view) const;

  [[nodiscard]] ChallengePlan build_challenge_plan(
      const InboundPacket& packet,
      std::string_view reason_code) const;
};

}  // namespace dasall::access
