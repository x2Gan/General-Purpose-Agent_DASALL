#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <type_traits>

#include "policy/PolicyTypes.h"
#include "support/TestAssertions.h"

namespace {

dasall::infra::policy::PolicyRuleDescriptor make_rule(std::string rule_id,
                                                      dasall::infra::policy::PolicyEffect effect,
                                                      std::uint32_t priority) {
  return dasall::infra::policy::PolicyRuleDescriptor{
      .rule_id = std::move(rule_id),
      .domain = dasall::infra::policy::PolicyDomain::DiagnosticsCommand,
      .subject = std::string("ops"),
      .action = std::string("execute"),
      .target_selector = std::string("diag.export"),
      .effect = effect,
      .priority = priority,
      .mode = dasall::infra::policy::PolicyMode::Enforced,
      .conditions = {std::string("profile=edge_balanced")},
      .reason_code = std::string("diag_policy_guard"),
  };
}

void test_policy_snapshot_freezes_generation_and_last_known_good_linkage() {
  using dasall::infra::policy::PolicyEffect;
  using dasall::infra::policy::PolicyMode;
  using dasall::infra::policy::PolicySnapshot;
  using dasall::tests::support::assert_true;

  const PolicySnapshot current{
      .snapshot_id = std::string("snapshot-002"),
      .generation = 2,
      .version = std::string("2026.04.01"),
      .mode = PolicyMode::Enforced,
      .effective_rules = {make_rule("deny-diag-export", PolicyEffect::Deny, 0)},
      .created_at = std::string("2026-04-01T12:00:00Z"),
      .source_chain = {std::string("defaults"), std::string("profile")},
      .last_known_good_ref = std::string("snapshot-001"),
  };

  const PolicySnapshot next{
      .snapshot_id = std::string("snapshot-003"),
      .generation = 3,
      .version = std::string("2026.04.01+patch1"),
      .mode = PolicyMode::Enforced,
      .effective_rules = {make_rule("deny-diag-export", PolicyEffect::Deny, 0),
                          make_rule("allow-diag-export", PolicyEffect::Allow, 10)},
      .created_at = std::string("2026-04-01T12:05:00Z"),
      .source_chain = {std::string("defaults"), std::string("profile"),
                       std::string("patch:ops-user")},
      .last_known_good_ref = current.snapshot_id,
  };

  const PolicySnapshot missing_source_chain{
      .snapshot_id = std::string("snapshot-004"),
      .generation = 4,
      .version = std::string("2026.04.01+patch2"),
      .mode = PolicyMode::Enforced,
      .effective_rules = next.effective_rules,
      .created_at = std::string("2026-04-01T12:10:00Z"),
      .source_chain = {},
      .last_known_good_ref = next.snapshot_id,
  };

  assert_true(current.is_valid(),
              "current policy snapshot should remain valid once frozen snapshot fields are populated");
  assert_true(current.can_roll_back(),
              "current policy snapshot should advertise rollback readiness with an LKG reference");
  assert_true(next.is_valid(),
              "next policy snapshot should remain valid when rules, source chain, and version are present");
  assert_true(next.generation > current.generation,
              "policy snapshot generation should remain explicitly monotonic across commits");
  assert_true(next.last_known_good_ref == current.snapshot_id,
              "next policy snapshot should link back to the previous stable snapshot as LKG evidence");
  assert_true(next.can_roll_back(),
              "next policy snapshot should stay rollback-capable when last-known-good is available");
  assert_true(!missing_source_chain.is_valid(),
              "policy snapshot should reject activation when source_chain is missing even if rules are present");
}

void test_policy_operation_result_freezes_apply_rollback_and_error_info_fields() {
  using dasall::contracts::ErrorInfo;
  using dasall::contracts::ResultCode;
  using dasall::infra::policy::PolicyOpResult;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(PolicyOpResult{}.applied), bool>);
  static_assert(std::is_same_v<decltype(PolicyOpResult{}.error_info), std::optional<ErrorInfo>>);

  const auto applied = PolicyOpResult::success("snapshot-003", 3);
  const auto dry_run = PolicyOpResult::success("snapshot-003", 3, false, true);
  const auto rolled_back = PolicyOpResult::success("snapshot-002", 2, true, false);
  const auto failure = PolicyOpResult::failure(ResultCode::ValidationFieldMissing,
                                               "policy patch rejected",
                                               "policy.apply_patch",
                                               "ISecurityPolicyManager");

  assert_true(applied.applied,
              "successful policy operations should freeze an explicit applied=true marker");
  assert_true(!applied.rolled_back && !applied.dry_run,
              "plain apply success should not implicitly mark rollback or dry-run state");
  assert_true(applied.snapshot_id == "snapshot-003" && applied.generation == 3,
              "successful policy operations should preserve snapshot identity and generation references");
  assert_true(!applied.error_info.has_value() && applied.references_only_contract_error_types(),
              "successful policy operations should not require non-contract error payloads");
  assert_true(dry_run.applied && dry_run.dry_run,
              "dry-run policy operations should remain explicit successes while carrying dry_run state");
  assert_true(rolled_back.applied && rolled_back.rolled_back,
              "rollback success should remain explicit instead of overloading generic failure flags");
  assert_true(!failure.applied && failure.error_info.has_value(),
              "failed policy operations should expose error_info while leaving applied=false");
  assert_true(failure.references_only_contract_error_types(),
              "failed policy operations should remain aligned to contracts ResultCode/ErrorInfo only");
}

}  // namespace

int main() {
  try {
    test_policy_snapshot_freezes_generation_and_last_known_good_linkage();
    test_policy_operation_result_freezes_apply_rollback_and_error_info_fields();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}