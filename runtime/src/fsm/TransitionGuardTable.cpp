#include "TransitionGuardTable.h"

#include <optional>
#include <utility>
#include <vector>

namespace dasall::runtime::TransitionGuardTable {
namespace {

using dasall::contracts::CheckpointState;

struct TransitionRule {
  RuntimeState from_state = RuntimeState::Idle;
  RuntimeState to_state = RuntimeState::Idle;
  TransitionGuardRule guard;
  StateTransitionCheckpointHint checkpoint_strategy;
};

[[nodiscard]] TransitionGuardRule require_all(std::vector<TransitionGuardFact> guard_facts) {
  return TransitionGuardRule{
      .all_of = std::move(guard_facts),
      .any_of = {},
  };
}

[[nodiscard]] TransitionGuardRule require_any(std::vector<TransitionGuardFact> guard_facts) {
  return TransitionGuardRule{
      .all_of = {},
      .any_of = std::move(guard_facts),
  };
}

[[nodiscard]] TransitionGuardRule no_guards() {
  return TransitionGuardRule{
      .all_of = {},
      .any_of = {},
  };
}

[[nodiscard]] StateTransitionCheckpointHint checkpoint_hint(
    const CheckpointMutationKind mutation,
    const std::optional<CheckpointState> checkpoint_state = std::nullopt,
    const bool pending_action_required = false) {
  return StateTransitionCheckpointHint{
      .mutation = mutation,
      .checkpoint_state = checkpoint_state,
      .pending_action_required = pending_action_required,
  };
}

[[nodiscard]] const std::vector<TransitionRule>& transition_rules() {
  static const std::vector<TransitionRule> kRules = {
      TransitionRule{
          .from_state = RuntimeState::Idle,
          .to_state = RuntimeState::Receiving,
          .guard = require_all({TransitionGuardFact::AgentRequestAvailable,
                                TransitionGuardFact::FacadeInitialized}),
          .checkpoint_strategy = checkpoint_hint(CheckpointMutationKind::None),
      },
      TransitionRule{
          .from_state = RuntimeState::Receiving,
          .to_state = RuntimeState::Planning,
          .guard = require_all({TransitionGuardFact::RequestValidated,
                                TransitionGuardFact::SessionLoaded,
                                TransitionGuardFact::BudgetInitialized}),
          .checkpoint_strategy =
              checkpoint_hint(CheckpointMutationKind::Write, CheckpointState::Running),
      },
      TransitionRule{
          .from_state = RuntimeState::Planning,
          .to_state = RuntimeState::Reasoning,
          .guard = require_all({TransitionGuardFact::ContextAssembled}),
          .checkpoint_strategy =
              checkpoint_hint(CheckpointMutationKind::Write, CheckpointState::Running),
      },
      TransitionRule{
          .from_state = RuntimeState::Reasoning,
          .to_state = RuntimeState::WaitingClarify,
          .guard = require_all({TransitionGuardFact::ClarificationNeeded,
                                TransitionGuardFact::ProfileAllowsClarify}),
          .checkpoint_strategy =
              checkpoint_hint(CheckpointMutationKind::Write, CheckpointState::Paused, true),
      },
      TransitionRule{
          .from_state = RuntimeState::WaitingClarify,
          .to_state = RuntimeState::Receiving,
          .guard = require_all({TransitionGuardFact::AgentRequestAvailable}),
          .checkpoint_strategy =
              checkpoint_hint(CheckpointMutationKind::Update, CheckpointState::Running),
      },
      TransitionRule{
          .from_state = RuntimeState::Reasoning,
          .to_state = RuntimeState::WaitingConfirm,
          .guard = require_all({TransitionGuardFact::HighRiskConfirmationRequired}),
          .checkpoint_strategy = checkpoint_hint(
              CheckpointMutationKind::Write,
              CheckpointState::WaitingConfirm,
              true),
      },
      TransitionRule{
          .from_state = RuntimeState::WaitingConfirm,
          .to_state = RuntimeState::ToolCalling,
          .guard = require_all({TransitionGuardFact::UserConfirmationGranted}),
          .checkpoint_strategy =
              checkpoint_hint(CheckpointMutationKind::Update, CheckpointState::Running),
      },
      TransitionRule{
          .from_state = RuntimeState::Reasoning,
          .to_state = RuntimeState::ToolCalling,
          .guard = require_all({TransitionGuardFact::ToolCallPlanned,
                                TransitionGuardFact::BudgetAllowsToolCall}),
          .checkpoint_strategy =
              checkpoint_hint(CheckpointMutationKind::Write, CheckpointState::Running),
      },
      TransitionRule{
          .from_state = RuntimeState::ToolCalling,
          .to_state = RuntimeState::WaitingExternal,
          .guard = require_all({TransitionGuardFact::ToolDispatchSubmitted}),
          .checkpoint_strategy = checkpoint_hint(
              CheckpointMutationKind::Write,
              CheckpointState::WaitingTool,
              true),
      },
      TransitionRule{
          .from_state = RuntimeState::WaitingExternal,
          .to_state = RuntimeState::Reflecting,
          .guard = require_all({TransitionGuardFact::ExternalResultAvailable}),
          .checkpoint_strategy =
              checkpoint_hint(CheckpointMutationKind::Update, CheckpointState::Running),
      },
      TransitionRule{
          .from_state = RuntimeState::Reflecting,
          .to_state = RuntimeState::Reasoning,
          .guard = require_any({TransitionGuardFact::ReflectionContinue,
                                TransitionGuardFact::RecoveryRetryAllowed,
                                TransitionGuardFact::RecoveryReplanAllowed}),
          .checkpoint_strategy =
              checkpoint_hint(CheckpointMutationKind::Write, CheckpointState::Running),
      },
      TransitionRule{
          .from_state = RuntimeState::Reflecting,
          .to_state = RuntimeState::FailedSafe,
          .guard = require_all({TransitionGuardFact::RecoveryAbortSafe}),
          .checkpoint_strategy =
            checkpoint_hint(CheckpointMutationKind::Write, CheckpointState::Failed),
        },
        TransitionRule{
          .from_state = RuntimeState::Reflecting,
          .to_state = RuntimeState::Degraded,
          .guard = require_all({TransitionGuardFact::RecoveryDegrade}),
          .checkpoint_strategy =
              checkpoint_hint(CheckpointMutationKind::Write, CheckpointState::Failed),
      },
      TransitionRule{
          .from_state = RuntimeState::Reasoning,
          .to_state = RuntimeState::Responding,
          .guard = require_all({TransitionGuardFact::DirectResponseReady}),
          .checkpoint_strategy =
              checkpoint_hint(CheckpointMutationKind::Write, CheckpointState::Running),
      },
      TransitionRule{
          .from_state = RuntimeState::FailedSafe,
          .to_state = RuntimeState::Responding,
          .guard = no_guards(),
          .checkpoint_strategy =
              checkpoint_hint(CheckpointMutationKind::Retain, CheckpointState::Failed),
      },
        TransitionRule{
          .from_state = RuntimeState::Degraded,
          .to_state = RuntimeState::Responding,
          .guard = no_guards(),
          .checkpoint_strategy =
            checkpoint_hint(CheckpointMutationKind::Retain, CheckpointState::Failed),
        },
        TransitionRule{
          .from_state = RuntimeState::SafeMode,
          .to_state = RuntimeState::Responding,
          .guard = no_guards(),
          .checkpoint_strategy =
            checkpoint_hint(CheckpointMutationKind::Retain, CheckpointState::Failed),
        },
      TransitionRule{
          .from_state = RuntimeState::Responding,
          .to_state = RuntimeState::Auditing,
          .guard = require_all({TransitionGuardFact::ResponseMaterialized}),
          .checkpoint_strategy = checkpoint_hint(CheckpointMutationKind::None),
      },
      TransitionRule{
          .from_state = RuntimeState::Auditing,
          .to_state = RuntimeState::Persisting,
          .guard = require_all({TransitionGuardFact::AuditCommitted}),
          .checkpoint_strategy = checkpoint_hint(CheckpointMutationKind::None),
      },
      TransitionRule{
          .from_state = RuntimeState::Persisting,
          .to_state = RuntimeState::Completed,
          .guard = require_all({TransitionGuardFact::PersistenceConfirmed}),
          .checkpoint_strategy =
              checkpoint_hint(CheckpointMutationKind::Write, CheckpointState::Succeeded),
      },
      TransitionRule{
          .from_state = RuntimeState::Completed,
          .to_state = RuntimeState::Idle,
          .guard = require_all({TransitionGuardFact::ResultReturned}),
          .checkpoint_strategy =
              checkpoint_hint(CheckpointMutationKind::ClearActiveReference),
      },
      TransitionRule{
          .from_state = RuntimeState::Reasoning,
          .to_state = RuntimeState::Failed,
          .guard = require_all({TransitionGuardFact::RecoveryRejected}),
          .checkpoint_strategy =
              checkpoint_hint(CheckpointMutationKind::Write, CheckpointState::Failed),
      },
      TransitionRule{
          .from_state = RuntimeState::WaitingExternal,
          .to_state = RuntimeState::Failed,
          .guard = require_all({TransitionGuardFact::RecoveryRejected}),
          .checkpoint_strategy =
              checkpoint_hint(CheckpointMutationKind::Write, CheckpointState::Failed),
      },
      TransitionRule{
          .from_state = RuntimeState::Failed,
          .to_state = RuntimeState::Degraded,
          .guard = require_all({TransitionGuardFact::DegradeAllowed}),
          .checkpoint_strategy =
              checkpoint_hint(CheckpointMutationKind::Retain, CheckpointState::Failed),
      },
      TransitionRule{
          .from_state = RuntimeState::Degraded,
          .to_state = RuntimeState::SafeMode,
          .guard = require_all({TransitionGuardFact::SafeModeTriggerSatisfied}),
          .checkpoint_strategy =
              checkpoint_hint(CheckpointMutationKind::Retain, CheckpointState::Failed),
      },
  };

  return kRules;
}

[[nodiscard]] const TransitionRule* find_rule(
    const RuntimeState from_state,
    const RuntimeState to_state) {
  for (const auto& rule : transition_rules()) {
    if (rule.from_state == from_state && rule.to_state == to_state) {
      return &rule;
    }
  }

  return nullptr;
}

}  // namespace

bool TransitionGuardRule::satisfied_by(const StateTransitionRequest& request) const {
  return !first_unsatisfied_guard(request).has_value();
}

std::optional<TransitionGuardFact> TransitionGuardRule::first_unsatisfied_guard(
    const StateTransitionRequest& request) const {
  for (const auto fact : all_of) {
    if (!request.has_guard(fact)) {
      return fact;
    }
  }

  if (!any_of.empty()) {
    for (const auto fact : any_of) {
      if (request.has_guard(fact)) {
        return std::nullopt;
      }
    }

    return any_of.front();
  }

  return std::nullopt;
}

bool is_legal(const RuntimeState from_state, const RuntimeState to_state) {
  return find_rule(from_state, to_state) != nullptr;
}

std::optional<TransitionGuardRule> get_guard(
    const RuntimeState from_state,
    const RuntimeState to_state) {
  const auto* rule = find_rule(from_state, to_state);
  if (rule == nullptr) {
    return std::nullopt;
  }

  return rule->guard;
}

std::optional<StateTransitionCheckpointHint> get_checkpoint_strategy(
    const RuntimeState from_state,
    const RuntimeState to_state) {
  const auto* rule = find_rule(from_state, to_state);
  if (rule == nullptr) {
    return std::nullopt;
  }

  return rule->checkpoint_strategy;
}

}  // namespace dasall::runtime::TransitionGuardTable