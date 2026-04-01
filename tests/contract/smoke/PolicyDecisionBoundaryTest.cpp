#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include "policy/ISecurityPolicyManager.h"
#include "policy/PolicyTypes.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

void test_policy_manager_interface_boundary_returns_only_frozen_policy_types() {
  using dasall::infra::policy::ISecurityPolicyManager;
  using dasall::infra::policy::PolicyBundle;
  using dasall::infra::policy::PolicyDecisionRef;
  using dasall::infra::policy::PolicyOpResult;
  using dasall::infra::policy::PolicyPatch;
  using dasall::infra::policy::PolicyQueryContext;
  using dasall::infra::policy::PolicySnapshot;
  using dasall::infra::policy::ValidationReport;
  using dasall::tests::support::assert_true;

  using LoadPolicySignature = PolicyOpResult (ISecurityPolicyManager::*)(const PolicyBundle&);
  using ApplyPatchSignature = PolicyOpResult (ISecurityPolicyManager::*)(const PolicyPatch&);
  using DryRunPatchSignature = ValidationReport (ISecurityPolicyManager::*)(const PolicyPatch&);
  using SnapshotSignature = PolicySnapshot (ISecurityPolicyManager::*)() const;
  using RollbackSignature = PolicyOpResult (ISecurityPolicyManager::*)(const std::string&);
  using EvaluateSignature = PolicyDecisionRef (ISecurityPolicyManager::*)(const PolicyQueryContext&) const;

  static_assert(std::is_same_v<decltype(&ISecurityPolicyManager::load_policy), LoadPolicySignature>);
  static_assert(std::is_same_v<decltype(&ISecurityPolicyManager::apply_patch), ApplyPatchSignature>);
  static_assert(std::is_same_v<decltype(&ISecurityPolicyManager::dry_run_patch), DryRunPatchSignature>);
  static_assert(std::is_same_v<decltype(&ISecurityPolicyManager::snapshot), SnapshotSignature>);
  static_assert(std::is_same_v<decltype(&ISecurityPolicyManager::rollback), RollbackSignature>);
  static_assert(std::is_same_v<decltype(&ISecurityPolicyManager::evaluate), EvaluateSignature>);

  assert_true(std::is_abstract_v<ISecurityPolicyManager>,
              "ISecurityPolicyManager should remain a pure abstract policy boundary without embedded implementation state");
  assert_true(std::has_virtual_destructor_v<ISecurityPolicyManager>,
              "ISecurityPolicyManager should keep a virtual destructor so the boundary can be consumed polymorphically");
}

void test_policy_decision_boundary_keeps_only_contract_aligned_decision_semantics() {
  using dasall::infra::policy::PolicyDecision;
  using dasall::infra::policy::PolicyDecisionRef;
  using dasall::infra::policy::PolicyQueryContext;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(PolicyDecisionRef{}.matched_rule_ids), std::vector<std::string>>);
  static_assert(std::is_same_v<decltype(PolicyDecisionRef{}.warnings), std::vector<std::string>>);

  const PolicyQueryContext query{
      .module = std::string("diagnostics"),
      .operation = std::string("execute"),
      .target_type = std::string("command"),
      .target_ref = std::string("export_snapshot"),
      .actor_ref = std::string("ops-user"),
  };

  assert_true(query.has_required_fields(),
              "policy query context should require only the frozen minimal boundary fields");

  const PolicyDecisionRef decision{
      .decision = PolicyDecision::RequireConfirmation,
      .reason_code = std::string("diag_confirmation_required"),
      .matched_rule_ids = {"require-confirmation-diag-export"},
      .snapshot_id = std::string("snapshot-010"),
      .generation = 10,
      .evidence_ref = std::string("audit:policy-change-001"),
      .warnings = {"compatibility_mode"},
  };

  assert_true(decision.is_valid(),
              "policy decision reference should remain valid with only decision, rule ids, snapshot, and evidence references");
}

void test_policy_operation_failures_stay_inside_contract_error_types() {
  using dasall::contracts::ErrorInfo;
  using dasall::contracts::ResultCode;
  using dasall::infra::policy::PolicyOpResult;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(PolicyOpResult{}.result_code), ResultCode>);
  static_assert(std::is_same_v<decltype(PolicyOpResult{}.error_info), std::optional<ErrorInfo>>);

  const auto failure = PolicyOpResult::failure(ResultCode::ValidationFieldMissing,
                                               "policy bundle is invalid",
                                               "policy.load",
                                               "ISecurityPolicyManager");

  assert_true(!failure.applied,
              "policy operation failures should remain explicit failures");
  assert_true(failure.error_info.has_value(),
              "policy operation failures should only carry contracts ErrorInfo on the failure path");
  assert_true(failure.references_only_contract_error_types(),
              "policy operation failures should remain within contracts ResultCode/ErrorInfo types");
}

}  // namespace

int main() {
  try {
    test_policy_manager_interface_boundary_returns_only_frozen_policy_types();
    test_policy_decision_boundary_keeps_only_contract_aligned_decision_semantics();
    test_policy_operation_failures_stay_inside_contract_error_types();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}