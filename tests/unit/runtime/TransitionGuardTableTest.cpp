#include <array>
#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "fsm/TransitionGuardTable.h"
#include "support/TestAssertions.h"

namespace {

[[nodiscard]] constexpr const char* checkpoint_state_name(
    const dasall::contracts::CheckpointState checkpoint_state) {
  using dasall::contracts::CheckpointState;

  switch (checkpoint_state) {
    case CheckpointState::Unspecified:
      return "Unspecified";
    case CheckpointState::Running:
      return "Running";
    case CheckpointState::Paused:
      return "Paused";
    case CheckpointState::WaitingConfirm:
      return "WaitingConfirm";
    case CheckpointState::WaitingTool:
      return "WaitingTool";
    case CheckpointState::Failed:
      return "Failed";
    case CheckpointState::Succeeded:
      return "Succeeded";
  }

  return "Unknown";
}

[[nodiscard]] constexpr const char* guard_fact_name(
    const dasall::runtime::TransitionGuardFact guard_fact) {
  using dasall::runtime::TransitionGuardFact;

  switch (guard_fact) {
    case TransitionGuardFact::AgentRequestAvailable:
      return "AgentRequestAvailable";
    case TransitionGuardFact::FacadeInitialized:
      return "FacadeInitialized";
    case TransitionGuardFact::RequestValidated:
      return "RequestValidated";
    case TransitionGuardFact::SessionLoaded:
      return "SessionLoaded";
    case TransitionGuardFact::BudgetInitialized:
      return "BudgetInitialized";
    case TransitionGuardFact::ContextAssembled:
      return "ContextAssembled";
    case TransitionGuardFact::ClarificationNeeded:
      return "ClarificationNeeded";
    case TransitionGuardFact::ProfileAllowsClarify:
      return "ProfileAllowsClarify";
    case TransitionGuardFact::HighRiskConfirmationRequired:
      return "HighRiskConfirmationRequired";
    case TransitionGuardFact::UserConfirmationGranted:
      return "UserConfirmationGranted";
    case TransitionGuardFact::ToolCallPlanned:
      return "ToolCallPlanned";
    case TransitionGuardFact::BudgetAllowsToolCall:
      return "BudgetAllowsToolCall";
    case TransitionGuardFact::ToolDispatchSubmitted:
      return "ToolDispatchSubmitted";
    case TransitionGuardFact::ExternalResultAvailable:
      return "ExternalResultAvailable";
    case TransitionGuardFact::ReflectionContinue:
      return "ReflectionContinue";
    case TransitionGuardFact::RecoveryRetryAllowed:
      return "RecoveryRetryAllowed";
    case TransitionGuardFact::RecoveryReplanAllowed:
      return "RecoveryReplanAllowed";
    case TransitionGuardFact::RecoveryAbortSafe:
      return "RecoveryAbortSafe";
    case TransitionGuardFact::RecoveryDegrade:
      return "RecoveryDegrade";
    case TransitionGuardFact::DirectResponseReady:
      return "DirectResponseReady";
    case TransitionGuardFact::ResponseMaterialized:
      return "ResponseMaterialized";
    case TransitionGuardFact::AuditCommitted:
      return "AuditCommitted";
    case TransitionGuardFact::PersistenceConfirmed:
      return "PersistenceConfirmed";
    case TransitionGuardFact::ResultReturned:
      return "ResultReturned";
    case TransitionGuardFact::RecoveryRejected:
      return "RecoveryRejected";
    case TransitionGuardFact::DegradeAllowed:
      return "DegradeAllowed";
    case TransitionGuardFact::SafeModeTriggerSatisfied:
      return "SafeModeTriggerSatisfied";
  }

  return "Unknown";
}

[[nodiscard]] constexpr const char* mutation_name(
    const dasall::runtime::CheckpointMutationKind mutation) {
  using dasall::runtime::CheckpointMutationKind;

  switch (mutation) {
    case CheckpointMutationKind::None:
      return "None";
    case CheckpointMutationKind::Write:
      return "Write";
    case CheckpointMutationKind::Update:
      return "Update";
    case CheckpointMutationKind::Retain:
      return "Retain";
    case CheckpointMutationKind::ClearActiveReference:
      return "ClearActiveReference";
  }

  return "Unknown";
}

[[nodiscard]] std::string describe_guards(
    const std::vector<dasall::runtime::TransitionGuardFact>& guard_facts) {
  std::string rendered;
  for (std::size_t index = 0; index < guard_facts.size(); ++index) {
    if (index != 0) {
      rendered += ",";
    }
    rendered += guard_fact_name(guard_facts[index]);
  }

  return rendered;
}

[[nodiscard]] std::string describe_checkpoint_state(
    const std::optional<dasall::contracts::CheckpointState>& checkpoint_state) {
  if (!checkpoint_state.has_value()) {
    return "nullopt";
  }

  return checkpoint_state_name(*checkpoint_state);
}

struct RuleCase {
  dasall::runtime::RuntimeState from_state;
  dasall::runtime::RuntimeState to_state;
  std::vector<dasall::runtime::TransitionGuardFact> all_of;
  std::vector<dasall::runtime::TransitionGuardFact> any_of;
  dasall::runtime::CheckpointMutationKind mutation;
  std::optional<dasall::contracts::CheckpointState> checkpoint_state;
  bool pending_action_required;
};

[[nodiscard]] dasall::runtime::StateTransitionRequest make_request(
    const RuleCase& rule_case,
    const bool satisfy_guards) {
  dasall::runtime::StateTransitionRequest request{
      .from_state = rule_case.from_state,
      .requested_to = rule_case.to_state,
      .transition_reason = "TransitionGuardTableTest",
    .guard_facts = {},
  };

  if (!satisfy_guards) {
    if (rule_case.all_of.empty() && rule_case.any_of.empty()) {
      return request;
    }

    if (rule_case.all_of.size() > 1) {
      request.guard_facts.assign(rule_case.all_of.begin() + 1, rule_case.all_of.end());
      return request;
    }

    request.guard_facts.clear();
    return request;
  }

  request.guard_facts = rule_case.all_of;
  if (!rule_case.any_of.empty()) {
    request.guard_facts.push_back(rule_case.any_of.front());
  }

  return request;
}

template <std::size_t N>
[[nodiscard]] bool is_expected_legal(
  const std::array<RuleCase, N>& rule_cases,
    const dasall::runtime::RuntimeState from_state,
    const dasall::runtime::RuntimeState to_state) {
  for (const auto& rule_case : rule_cases) {
    if (rule_case.from_state == from_state && rule_case.to_state == to_state) {
      return true;
    }
  }

  return false;
}

}  // namespace

int main() {
  using dasall::contracts::CheckpointState;
  using dasall::runtime::CheckpointMutationKind;
  using dasall::runtime::RuntimeState;
  using dasall::runtime::TransitionGuardFact;
  using dasall::runtime::TransitionGuardTable::get_checkpoint_strategy;
  using dasall::runtime::TransitionGuardTable::get_guard;
  using dasall::runtime::TransitionGuardTable::is_legal;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  try {
    const std::array<RuleCase, 25> kRuleCases{{
        {RuntimeState::Idle,
         RuntimeState::Receiving,
         {TransitionGuardFact::AgentRequestAvailable,
          TransitionGuardFact::FacadeInitialized},
         {},
         CheckpointMutationKind::None,
         std::nullopt,
         false},
        {RuntimeState::Receiving,
         RuntimeState::Planning,
         {TransitionGuardFact::RequestValidated,
          TransitionGuardFact::SessionLoaded,
          TransitionGuardFact::BudgetInitialized},
         {},
         CheckpointMutationKind::Write,
         CheckpointState::Running,
         false},
        {RuntimeState::Planning,
         RuntimeState::Reasoning,
         {TransitionGuardFact::ContextAssembled},
         {},
         CheckpointMutationKind::Write,
         CheckpointState::Running,
         false},
        {RuntimeState::Reasoning,
         RuntimeState::WaitingClarify,
         {TransitionGuardFact::ClarificationNeeded,
          TransitionGuardFact::ProfileAllowsClarify},
         {},
         CheckpointMutationKind::Write,
         CheckpointState::Paused,
         true},
        {RuntimeState::WaitingClarify,
         RuntimeState::Receiving,
         {TransitionGuardFact::AgentRequestAvailable},
         {},
         CheckpointMutationKind::Update,
         CheckpointState::Running,
         false},
        {RuntimeState::Reasoning,
         RuntimeState::WaitingConfirm,
         {TransitionGuardFact::HighRiskConfirmationRequired},
         {},
         CheckpointMutationKind::Write,
         CheckpointState::WaitingConfirm,
         true},
        {RuntimeState::WaitingConfirm,
         RuntimeState::ToolCalling,
         {TransitionGuardFact::UserConfirmationGranted},
         {},
         CheckpointMutationKind::Update,
         CheckpointState::Running,
         false},
        {RuntimeState::Reasoning,
         RuntimeState::ToolCalling,
         {TransitionGuardFact::ToolCallPlanned,
          TransitionGuardFact::BudgetAllowsToolCall},
         {},
         CheckpointMutationKind::Write,
         CheckpointState::Running,
         false},
        {RuntimeState::ToolCalling,
         RuntimeState::WaitingExternal,
         {TransitionGuardFact::ToolDispatchSubmitted},
         {},
         CheckpointMutationKind::Write,
         CheckpointState::WaitingTool,
         true},
        {RuntimeState::WaitingExternal,
         RuntimeState::Reflecting,
         {TransitionGuardFact::ExternalResultAvailable},
         {},
         CheckpointMutationKind::Update,
         CheckpointState::Running,
         false},
        {RuntimeState::Reflecting,
         RuntimeState::Reasoning,
         {},
         {TransitionGuardFact::ReflectionContinue,
          TransitionGuardFact::RecoveryRetryAllowed,
          TransitionGuardFact::RecoveryReplanAllowed},
         CheckpointMutationKind::Write,
         CheckpointState::Running,
         false},
        {RuntimeState::Reflecting,
         RuntimeState::FailedSafe,
         {TransitionGuardFact::RecoveryAbortSafe},
         {},
         CheckpointMutationKind::Write,
         CheckpointState::Failed,
         false},
        {RuntimeState::Reflecting,
         RuntimeState::Degraded,
         {TransitionGuardFact::RecoveryDegrade},
         {},
         CheckpointMutationKind::Write,
         CheckpointState::Failed,
         false},
        {RuntimeState::Reasoning,
         RuntimeState::Responding,
         {TransitionGuardFact::DirectResponseReady},
         {},
         CheckpointMutationKind::Write,
         CheckpointState::Running,
         false},
        {RuntimeState::FailedSafe,
         RuntimeState::Responding,
         {},
         {},
         CheckpointMutationKind::Retain,
         CheckpointState::Failed,
         false},
        {RuntimeState::Degraded,
         RuntimeState::Responding,
         {},
         {},
         CheckpointMutationKind::Retain,
         CheckpointState::Failed,
         false},
        {RuntimeState::SafeMode,
         RuntimeState::Responding,
         {},
         {},
         CheckpointMutationKind::Retain,
         CheckpointState::Failed,
         false},
        {RuntimeState::Responding,
         RuntimeState::Auditing,
         {TransitionGuardFact::ResponseMaterialized},
         {},
         CheckpointMutationKind::None,
         std::nullopt,
         false},
        {RuntimeState::Auditing,
         RuntimeState::Persisting,
         {TransitionGuardFact::AuditCommitted},
         {},
         CheckpointMutationKind::None,
         std::nullopt,
         false},
        {RuntimeState::Persisting,
         RuntimeState::Completed,
         {TransitionGuardFact::PersistenceConfirmed},
         {},
         CheckpointMutationKind::Write,
         CheckpointState::Succeeded,
         false},
        {RuntimeState::Completed,
         RuntimeState::Idle,
         {TransitionGuardFact::ResultReturned},
         {},
         CheckpointMutationKind::ClearActiveReference,
         std::nullopt,
         false},
        {RuntimeState::Reasoning,
         RuntimeState::Failed,
         {TransitionGuardFact::RecoveryRejected},
         {},
         CheckpointMutationKind::Write,
         CheckpointState::Failed,
         false},
        {RuntimeState::WaitingExternal,
         RuntimeState::Failed,
         {TransitionGuardFact::RecoveryRejected},
         {},
         CheckpointMutationKind::Write,
         CheckpointState::Failed,
         false},
        {RuntimeState::Failed,
         RuntimeState::Degraded,
         {TransitionGuardFact::DegradeAllowed},
         {},
         CheckpointMutationKind::Retain,
         CheckpointState::Failed,
         false},
        {RuntimeState::Degraded,
         RuntimeState::SafeMode,
         {TransitionGuardFact::SafeModeTriggerSatisfied},
         {},
         CheckpointMutationKind::Retain,
         CheckpointState::Failed,
         false},
    }};

    for (const auto& rule_case : kRuleCases) {
      assert_true(
          is_legal(rule_case.from_state, rule_case.to_state),
          std::string("expected legal transition: ") +
              dasall::runtime::runtime_state_name(rule_case.from_state) + " -> " +
              dasall::runtime::runtime_state_name(rule_case.to_state));

      const auto guard_rule = get_guard(rule_case.from_state, rule_case.to_state);
      assert_true(
          guard_rule.has_value(),
          std::string("expected guard rule for legal transition: ") +
              dasall::runtime::runtime_state_name(rule_case.from_state) + " -> " +
              dasall::runtime::runtime_state_name(rule_case.to_state));
      assert_equal(
          describe_guards(rule_case.all_of),
          describe_guards(guard_rule->all_of),
          std::string("unexpected all_of guards for transition ") +
              dasall::runtime::runtime_state_name(rule_case.from_state) + " -> " +
              dasall::runtime::runtime_state_name(rule_case.to_state));
      assert_equal(
          describe_guards(rule_case.any_of),
          describe_guards(guard_rule->any_of),
          std::string("unexpected any_of guards for transition ") +
              dasall::runtime::runtime_state_name(rule_case.from_state) + " -> " +
              dasall::runtime::runtime_state_name(rule_case.to_state));

      const auto satisfied_request = make_request(rule_case, true);
      assert_true(
          guard_rule->satisfied_by(satisfied_request),
          std::string("expected guard to accept satisfied request for transition ") +
              dasall::runtime::runtime_state_name(rule_case.from_state) + " -> " +
              dasall::runtime::runtime_state_name(rule_case.to_state));
      assert_true(
          !guard_rule->first_unsatisfied_guard(satisfied_request).has_value(),
          std::string("satisfied request should not produce violated guard for transition ") +
              dasall::runtime::runtime_state_name(rule_case.from_state) + " -> " +
              dasall::runtime::runtime_state_name(rule_case.to_state));

      const auto missing_request = make_request(rule_case, false);
      if (!rule_case.all_of.empty() || !rule_case.any_of.empty()) {
        const auto violated_guard = guard_rule->first_unsatisfied_guard(missing_request);
        assert_true(
            violated_guard.has_value(),
            std::string("missing guard request should report violated guard for transition ") +
                dasall::runtime::runtime_state_name(rule_case.from_state) + " -> " +
                dasall::runtime::runtime_state_name(rule_case.to_state));

        const auto expected_violated_guard = !rule_case.all_of.empty() ? rule_case.all_of.front()
                                                                       : rule_case.any_of.front();
        assert_equal(
            std::string(guard_fact_name(expected_violated_guard)),
            std::string(guard_fact_name(*violated_guard)),
            std::string("unexpected violated guard for transition ") +
                dasall::runtime::runtime_state_name(rule_case.from_state) + " -> " +
                dasall::runtime::runtime_state_name(rule_case.to_state));
        assert_true(
            !guard_rule->satisfied_by(missing_request),
            std::string("missing guard request should be rejected for transition ") +
                dasall::runtime::runtime_state_name(rule_case.from_state) + " -> " +
                dasall::runtime::runtime_state_name(rule_case.to_state));
      } else {
        assert_true(
            !guard_rule->first_unsatisfied_guard(missing_request).has_value(),
            std::string("guardless transition should not report violated guard for transition ") +
                dasall::runtime::runtime_state_name(rule_case.from_state) + " -> " +
                dasall::runtime::runtime_state_name(rule_case.to_state));
      }

      const auto checkpoint_strategy =
          get_checkpoint_strategy(rule_case.from_state, rule_case.to_state);
      assert_true(
          checkpoint_strategy.has_value(),
          std::string("expected checkpoint strategy for legal transition: ") +
              dasall::runtime::runtime_state_name(rule_case.from_state) + " -> " +
              dasall::runtime::runtime_state_name(rule_case.to_state));
      assert_equal(
          std::string(mutation_name(rule_case.mutation)),
          std::string(mutation_name(checkpoint_strategy->mutation)),
          std::string("unexpected checkpoint mutation for transition ") +
              dasall::runtime::runtime_state_name(rule_case.from_state) + " -> " +
              dasall::runtime::runtime_state_name(rule_case.to_state));
      assert_equal(
          describe_checkpoint_state(rule_case.checkpoint_state),
          describe_checkpoint_state(checkpoint_strategy->checkpoint_state),
          std::string("unexpected checkpoint state for transition ") +
              dasall::runtime::runtime_state_name(rule_case.from_state) + " -> " +
              dasall::runtime::runtime_state_name(rule_case.to_state));
      assert_true(
          checkpoint_strategy->pending_action_required == rule_case.pending_action_required,
          std::string("unexpected pending_action_required for transition ") +
              dasall::runtime::runtime_state_name(rule_case.from_state) + " -> " +
              dasall::runtime::runtime_state_name(rule_case.to_state));
    }

    for (const auto from_state : dasall::runtime::runtime_state_catalog()) {
      for (const auto to_state : dasall::runtime::runtime_state_catalog()) {
        if (is_expected_legal(kRuleCases, from_state, to_state)) {
          continue;
        }

        assert_true(
            !is_legal(from_state, to_state),
            std::string("unexpected illegal transition admitted by table: ") +
                dasall::runtime::runtime_state_name(from_state) + " -> " +
                dasall::runtime::runtime_state_name(to_state));
        assert_true(
            !get_guard(from_state, to_state).has_value(),
            std::string("unexpected guard returned for illegal transition: ") +
                dasall::runtime::runtime_state_name(from_state) + " -> " +
                dasall::runtime::runtime_state_name(to_state));
        assert_true(
            !get_checkpoint_strategy(from_state, to_state).has_value(),
            std::string("unexpected checkpoint strategy returned for illegal transition: ") +
                dasall::runtime::runtime_state_name(from_state) + " -> " +
                dasall::runtime::runtime_state_name(to_state));
      }
    }
  } catch (const std::exception& ex) {
    std::cerr << "TransitionGuardTableTest failed: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}