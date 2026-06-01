#include "validation/StageSchemaRegistry.h"

namespace dasall::cognition::validation {
namespace {

const StageSchemaSpec kPlanningPlanSchema = {
    .stage_name = "planning",
    .schema_version = "cognition.plan.v1",
    .known_top_level_fields = {
        "schema_version",
        "plan_id",
        "revision",
        "nodes",
        "edges",
        "open_questions",
        "plan_rationale",
        "estimated_complexity",
    },
    .required_fields = {
        "schema_version",
        "plan_id",
        "revision",
        "nodes",
        "edges",
        "plan_rationale",
        "estimated_complexity",
        "nodes.node_id",
        "nodes.objective",
        "nodes.success_signal",
        "nodes.action_kind_hint",
    },
    .enum_constraints = {
        EnumConstraint{.field_path = "schema_version",
                       .allowed_values = {"cognition.plan.v1"}},
        EnumConstraint{.field_path = "nodes.action_kind_hint",
                       .allowed_values = {
                           "tool_action",
                           "direct_response",
                           "validation",
                           "clarification",
                       }},
    },
    .numeric_bounds = {
        NumericConstraint{.field_path = "revision", .min_value = 0.0, .max_value = std::nullopt},
        NumericConstraint{.field_path = "estimated_complexity",
                          .min_value = 0.0,
                          .max_value = std::nullopt},
    },
    .list_constraints = {
        ListConstraint{.field_path = "nodes", .min_items = 1U, .max_items = std::nullopt},
    },
    .stage_specific_invariants = {
        "plan_graph.known_node_ids",
        "plan_graph.acyclic",
        "plan_graph.max_plan_nodes",
        "plan_graph.max_plan_depth",
    },
    .unknown_field_policy = UnknownFieldPolicy::AllowRegisteredExtensions,
    .allowed_extension_prefixes = {"x_"},
};

const StageSchemaSpec kExecutionActionDecisionSchema = {
    .stage_name = "execution",
    .schema_version = "cognition.reasoning.v1",
    .known_top_level_fields = {
        "schema_version",
        "decision_kind",
        "confidence",
        "rationale",
        "selected_node_id",
        "tool_intent_hint",
        "clarification_needed",
        "clarification_question",
        "response_outline",
        "candidate_scores",
    },
    .required_fields = {
        "schema_version",
        "decision_kind",
        "confidence",
        "rationale",
        "selected_node_id",
        "tool_intent_hint",
        "clarification_needed",
        "clarification_question",
        "response_outline",
        "candidate_scores",
    },
    .enum_constraints = {
        EnumConstraint{.field_path = "schema_version",
                       .allowed_values = {"cognition.reasoning.v1"}},
        EnumConstraint{.field_path = "decision_kind",
                       .allowed_values = {
                           "ExecuteAction",
                           "DirectResponse",
                           "AskClarification",
                           "ConvergeSafe",
                           "NoDecision",
                       }},
    },
    .numeric_bounds = {
        NumericConstraint{.field_path = "confidence", .min_value = 0.0, .max_value = 1.0},
    },
    .list_constraints = {
        ListConstraint{.field_path = "candidate_scores", .min_items = 1U, .max_items = 4U},
    },
    .stage_specific_invariants = {
        "action_decision.execute_action_requires_node_and_tool",
        "action_decision.ask_clarification_requires_question",
        "action_decision.response_requires_outline",
    },
    .unknown_field_policy = UnknownFieldPolicy::AllowRegisteredExtensions,
    .allowed_extension_prefixes = {"x_"},
};

const StageSchemaSpec kReflectionDecisionSchema = {
    .stage_name = "reflection",
    .schema_version = "cognition.reflection.v1",
    .known_top_level_fields = {
        "schema_version",
        "request_id",
        "decision_kind",
        "rationale",
        "goal_id",
        "confidence",
        "relevant_observation_refs",
        "hint_ref",
        "created_at",
        "tags",
    },
    .required_fields = {
        "schema_version",
        "request_id",
        "decision_kind",
        "rationale",
    },
    .enum_constraints = {
        EnumConstraint{.field_path = "schema_version",
                       .allowed_values = {"cognition.reflection.v1"}},
        EnumConstraint{.field_path = "decision_kind",
                       .allowed_values = {
                           "Continue",
                           "RetryStep",
                           "Replan",
                           "AbortSafe",
                       }},
    },
    .numeric_bounds = {
        NumericConstraint{.field_path = "confidence", .min_value = 0.0, .max_value = 1.0},
        NumericConstraint{.field_path = "created_at", .min_value = 1.0, .max_value = std::nullopt},
    },
    .list_constraints = {},
    .stage_specific_invariants = {
        "reflection_decision.field_rules",
    },
    .unknown_field_policy = UnknownFieldPolicy::AllowRegisteredExtensions,
    .allowed_extension_prefixes = {"x_"},
};

const StageSchemaSpec kResponseEnvelopeSchema = {
    .stage_name = "response",
    .schema_version = "cognition.response.v1",
    .known_top_level_fields = {
        "schema_version",
        "response_mode",
        "summary_text",
        "structured_sections",
        "omitted_details",
        "fallback_used",
    },
    .required_fields = {
        "schema_version",
        "response_mode",
        "summary_text",
        "structured_sections",
        "omitted_details",
        "fallback_used",
    },
    .enum_constraints = {
        EnumConstraint{.field_path = "schema_version",
                       .allowed_values = {"cognition.response.v1"}},
        EnumConstraint{.field_path = "response_mode",
                       .allowed_values = {
                           "llm_bridge",
                           "observation_projection",
                           "template_fallback",
                           "unavailable",
                       }},
    },
    .numeric_bounds = {},
    .list_constraints = {},
    .stage_specific_invariants = {
        "response_envelope.summary_text_present",
        "response_envelope.completed_status_consistent",
    },
    .unknown_field_policy = UnknownFieldPolicy::AllowRegisteredExtensions,
    .allowed_extension_prefixes = {"x_"},
};

}  // namespace

const StageSchemaSpec& schema_for_planning_plan() {
  return kPlanningPlanSchema;
}

const StageSchemaSpec& schema_for_execution_action_decision() {
  return kExecutionActionDecisionSchema;
}

const StageSchemaSpec& schema_for_reflection_decision() {
    return kReflectionDecisionSchema;
}

const StageSchemaSpec& schema_for_response_envelope() {
    return kResponseEnvelopeSchema;
}

}  // namespace dasall::cognition::validation