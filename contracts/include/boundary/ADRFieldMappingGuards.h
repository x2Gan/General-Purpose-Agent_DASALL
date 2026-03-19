#pragma once

#include <array>
#include <cstddef>
#include <string_view>

#include "boundary/MultiAgentBoundaryGuards.h"
#include "boundary/RecoveryBoundaryGuards.h"
#include "checkpoint/RecoveryRequestGuards.h"
#include "prompt/PromptBoundaryContracts.h"
#include "task/WorkerLeaseGuards.h"

namespace dasall::contracts {

enum class ADRIdentifier {
  ADR006,
  ADR007,
  ADR008,
};

enum class ADRMappedObject {
  ContextPacket,
  PromptComposeRequest,
  PromptComposeResult,
  ReflectionDecision,
  RecoveryRequest,
  RecoveryOutcome,
  MultiAgentRequest,
  MultiAgentResult,
  WorkerTask,
  WorkerLease,
};

struct ADRFieldObjectMappingEntry {
  ADRIdentifier adr;
  ADRMappedObject object;
  std::string_view object_name;
  std::string_view wp_range;
  std::string_view primary_guard_symbol;
  std::string_view boundary_guard_symbol;
};

struct ADRForbiddenFieldMappingEntry {
  ADRIdentifier adr;
  ADRMappedObject object;
  std::string_view object_name;
  std::string_view field_name;
  std::string_view guard_symbol;
  std::string_view rationale;
};

struct ADRFieldMappingValidationResult {
  bool ok = false;
  bool object_catalog_complete = false;
  bool forbidden_field_catalog_complete = false;
  bool guard_dispatch_complete = false;
  bool no_duplicate_entries = false;
  std::string_view first_failed_check = "not-run";
  std::string_view reason = "ADR field mapping validation not yet run";
};

inline constexpr std::size_t kADR006ObjectMappingCount = 3;
inline constexpr std::size_t kADR007ObjectMappingCount = 3;
inline constexpr std::size_t kADR008ObjectMappingCount = 4;

inline constexpr std::size_t kADR006ForbiddenFieldMappingCount = 17;
inline constexpr std::size_t kADR007ForbiddenFieldMappingCount = 24;
inline constexpr std::size_t kADR008ForbiddenFieldMappingCount = 16;

inline constexpr std::array<ADRFieldObjectMappingEntry, 10>
    kADRFieldObjectMappingCatalog = {{
        {
            ADRIdentifier::ADR006,
            ADRMappedObject::ContextPacket,
            "ContextPacket",
            "WP03-T010/T011",
            "ContextPacketGuards.h",
            "evaluate_context_packet_prompt_field_boundary",
        },
        {
            ADRIdentifier::ADR006,
            ADRMappedObject::PromptComposeRequest,
            "PromptComposeRequest",
            "WP04-T002/T003",
            "validate_prompt_compose_request_field_rules",
            "evaluate_compose_request_field_boundary",
        },
        {
            ADRIdentifier::ADR006,
            ADRMappedObject::PromptComposeResult,
            "PromptComposeResult",
            "WP04-T004/T005",
            "validate_prompt_compose_result_field_rules",
            "evaluate_compose_result_field_boundary",
        },
        {
            ADRIdentifier::ADR007,
            ADRMappedObject::ReflectionDecision,
            "ReflectionDecision",
            "WP04-T007/T008",
            "validate_reflection_decision_field_rules",
            "evaluate_reflection_decision_field_boundary",
        },
        {
            ADRIdentifier::ADR007,
            ADRMappedObject::RecoveryRequest,
            "RecoveryRequest",
            "WP04-T009/T010",
            "validate_recovery_request_field_rules",
            "evaluate_recovery_request_field_boundary",
        },
        {
            ADRIdentifier::ADR007,
            ADRMappedObject::RecoveryOutcome,
            "RecoveryOutcome",
            "WP04-T011/T012",
            "validate_recovery_outcome_field_rules",
            "evaluate_recovery_outcome_field_boundary",
        },
        {
            ADRIdentifier::ADR008,
            ADRMappedObject::MultiAgentRequest,
            "MultiAgentRequest",
            "WP04-T014/T015",
            "validate_multi_agent_request_field_rules",
            "evaluate_multi_agent_request_field_boundary",
        },
        {
            ADRIdentifier::ADR008,
            ADRMappedObject::MultiAgentResult,
            "MultiAgentResult",
            "WP04-T016/T017",
            "validate_multi_agent_result_field_rules",
            "evaluate_multi_agent_result_field_boundary",
        },
        {
            ADRIdentifier::ADR008,
            ADRMappedObject::WorkerTask,
            "WorkerTask",
            "WP04-T018/T019",
            "validate_worker_task_field_rules",
            "evaluate_worker_task_field_boundary",
        },
        {
            ADRIdentifier::ADR008,
            ADRMappedObject::WorkerLease,
            "WorkerLease",
            "WP04-T020/T021",
            "validate_worker_lease_field_rules",
            "validate_worker_lease_forbidden_field",
        },
    }};

inline constexpr std::array<ADRForbiddenFieldMappingEntry, 57>
    kADRForbiddenFieldMappingCatalog = {{
        {ADRIdentifier::ADR006, ADRMappedObject::ContextPacket, "ContextPacket", "final_messages", "evaluate_context_packet_prompt_field_boundary", "message-rendering output must not enter ContextPacket"},
        {ADRIdentifier::ADR006, ADRMappedObject::ContextPacket, "ContextPacket", "provider_payload", "evaluate_context_packet_prompt_field_boundary", "provider payload must not enter ContextPacket"},
        {ADRIdentifier::ADR006, ADRMappedObject::ContextPacket, "ContextPacket", "rendered_prompt", "evaluate_context_packet_prompt_field_boundary", "rendered prompt must not enter ContextPacket"},
        {ADRIdentifier::ADR006, ADRMappedObject::ContextPacket, "ContextPacket", "prompt_bundle", "evaluate_context_packet_prompt_field_boundary", "prompt bundle must not enter ContextPacket"},
        {ADRIdentifier::ADR006, ADRMappedObject::ContextPacket, "ContextPacket", "system_instructions", "evaluate_context_packet_prompt_field_boundary", "system instructions belong to prompt composition"},
        {ADRIdentifier::ADR006, ADRMappedObject::ContextPacket, "ContextPacket", "few_shots", "evaluate_context_packet_prompt_field_boundary", "few-shot examples belong to prompt composition"},
        {ADRIdentifier::ADR006, ADRMappedObject::ContextPacket, "ContextPacket", "output_schema", "evaluate_context_packet_prompt_field_boundary", "output schema belongs to prompt composition"},
        {ADRIdentifier::ADR006, ADRMappedObject::ContextPacket, "ContextPacket", "tool_schemas", "evaluate_context_packet_prompt_field_boundary", "tool schemas belong to prompt composition or prompt policy"},
        {ADRIdentifier::ADR006, ADRMappedObject::PromptComposeRequest, "PromptComposeRequest", "memory_snapshot", "evaluate_compose_request_field_boundary", "compose request must not own memory snapshot data"},
        {ADRIdentifier::ADR006, ADRMappedObject::PromptComposeRequest, "PromptComposeRequest", "retrieval_candidates", "evaluate_compose_request_field_boundary", "compose request must not own retrieval candidates"},
        {ADRIdentifier::ADR006, ADRMappedObject::PromptComposeRequest, "PromptComposeRequest", "context_packet_internal", "evaluate_compose_request_field_boundary", "compose request must not expose internal context packet data"},
        {ADRIdentifier::ADR006, ADRMappedObject::PromptComposeRequest, "PromptComposeRequest", "knowledge_fragments", "evaluate_compose_request_field_boundary", "compose request must not own raw knowledge fragments"},
        {ADRIdentifier::ADR006, ADRMappedObject::PromptComposeResult, "PromptComposeResult", "memory_write_back", "evaluate_compose_result_field_boundary", "compose result must not write back to memory"},
        {ADRIdentifier::ADR006, ADRMappedObject::PromptComposeResult, "PromptComposeResult", "context_update", "evaluate_compose_result_field_boundary", "compose result must not update ContextPacket"},
        {ADRIdentifier::ADR006, ADRMappedObject::PromptComposeResult, "PromptComposeResult", "belief_patch", "evaluate_compose_result_field_boundary", "compose result must not patch belief state"},
        {ADRIdentifier::ADR006, ADRMappedObject::PromptComposeResult, "PromptComposeResult", "knowledge_recall", "evaluate_compose_result_field_boundary", "compose result must not trigger knowledge recall"},
        {ADRIdentifier::ADR006, ADRMappedObject::PromptComposeResult, "PromptComposeResult", "history_update", "evaluate_compose_result_field_boundary", "compose result must not write conversation history"},
        {ADRIdentifier::ADR007, ADRMappedObject::ReflectionDecision, "ReflectionDecision", "retry_after_ms", "evaluate_reflection_decision_field_boundary", "reflection decision must not contain retry scheduling"},
        {ADRIdentifier::ADR007, ADRMappedObject::ReflectionDecision, "ReflectionDecision", "backoff_strategy", "evaluate_reflection_decision_field_boundary", "reflection decision must not contain backoff policy"},
        {ADRIdentifier::ADR007, ADRMappedObject::ReflectionDecision, "ReflectionDecision", "lease_extension", "evaluate_reflection_decision_field_boundary", "reflection decision must not extend leases"},
        {ADRIdentifier::ADR007, ADRMappedObject::ReflectionDecision, "ReflectionDecision", "checkpoint_blob", "evaluate_reflection_decision_field_boundary", "reflection decision must not carry checkpoint payloads"},
        {ADRIdentifier::ADR007, ADRMappedObject::ReflectionDecision, "ReflectionDecision", "circuit_breaker_transition", "evaluate_reflection_decision_field_boundary", "reflection decision must not change circuit breaker state"},
        {ADRIdentifier::ADR007, ADRMappedObject::RecoveryRequest, "RecoveryRequest", "decision_kind", "evaluate_recovery_request_field_boundary", "recovery request must not duplicate reflection top-level semantics"},
        {ADRIdentifier::ADR007, ADRMappedObject::RecoveryRequest, "RecoveryRequest", "rationale", "evaluate_recovery_request_field_boundary", "recovery request must not duplicate reflection rationale"},
        {ADRIdentifier::ADR007, ADRMappedObject::RecoveryRequest, "RecoveryRequest", "confidence", "evaluate_recovery_request_field_boundary", "recovery request must not duplicate reflection confidence"},
        {ADRIdentifier::ADR007, ADRMappedObject::RecoveryRequest, "RecoveryRequest", "relevant_observation_refs", "evaluate_recovery_request_field_boundary", "recovery request must not duplicate reflection evidence refs"},
        {ADRIdentifier::ADR007, ADRMappedObject::RecoveryRequest, "RecoveryRequest", "hint_ref", "evaluate_recovery_request_field_boundary", "recovery request must not duplicate reflection hint refs"},
        {ADRIdentifier::ADR007, ADRMappedObject::RecoveryRequest, "RecoveryRequest", "executed_action", "evaluate_recovery_request_field_boundary", "recovery request must not contain outcome action"},
        {ADRIdentifier::ADR007, ADRMappedObject::RecoveryRequest, "RecoveryRequest", "final_runtime_state", "evaluate_recovery_request_field_boundary", "recovery request must not contain outcome runtime state"},
        {ADRIdentifier::ADR007, ADRMappedObject::RecoveryRequest, "RecoveryRequest", "updated_retry_counters", "evaluate_recovery_request_field_boundary", "recovery request must not contain outcome retry counters"},
        {ADRIdentifier::ADR007, ADRMappedObject::RecoveryRequest, "RecoveryRequest", "checkpoint_ref", "evaluate_recovery_request_field_boundary", "recovery request must not contain outcome checkpoint refs"},
        {ADRIdentifier::ADR007, ADRMappedObject::RecoveryRequest, "RecoveryRequest", "compensation_result_ref", "evaluate_recovery_request_field_boundary", "recovery request must not contain compensation result refs"},
        {ADRIdentifier::ADR007, ADRMappedObject::RecoveryRequest, "RecoveryRequest", "rejection_reason", "evaluate_recovery_request_field_boundary", "recovery request must not contain outcome rejection reason"},
        {ADRIdentifier::ADR007, ADRMappedObject::RecoveryRequest, "RecoveryRequest", "escalation_reason", "evaluate_recovery_request_field_boundary", "recovery request must not contain outcome escalation reason"},
        {ADRIdentifier::ADR007, ADRMappedObject::RecoveryRequest, "RecoveryRequest", "retry_after_ms", "evaluate_recovery_request_field_boundary", "recovery request must not contain execution scheduling outputs"},
        {ADRIdentifier::ADR007, ADRMappedObject::RecoveryRequest, "RecoveryRequest", "backoff_strategy", "evaluate_recovery_request_field_boundary", "recovery request must not contain execution scheduling outputs"},
        {ADRIdentifier::ADR007, ADRMappedObject::RecoveryRequest, "RecoveryRequest", "circuit_breaker_transition", "evaluate_recovery_request_field_boundary", "recovery request must not contain execution scheduling outputs"},
        {ADRIdentifier::ADR007, ADRMappedObject::RecoveryOutcome, "RecoveryOutcome", "failure_root_cause", "evaluate_recovery_outcome_field_boundary", "recovery outcome must not contain failure attribution"},
        {ADRIdentifier::ADR007, ADRMappedObject::RecoveryOutcome, "RecoveryOutcome", "root_cause_analysis", "evaluate_recovery_outcome_field_boundary", "recovery outcome must not contain root-cause analysis"},
        {ADRIdentifier::ADR007, ADRMappedObject::RecoveryOutcome, "RecoveryOutcome", "belief_patch", "evaluate_recovery_outcome_field_boundary", "recovery outcome must not patch belief state"},
        {ADRIdentifier::ADR007, ADRMappedObject::RecoveryOutcome, "RecoveryOutcome", "plan_patch_hint", "evaluate_recovery_outcome_field_boundary", "recovery outcome must not patch the plan"},
        {ADRIdentifier::ADR008, ADRMappedObject::MultiAgentRequest, "MultiAgentRequest", "agent_request", "evaluate_multi_agent_request_field_boundary", "multi-agent request must not reuse AgentRequest"},
        {ADRIdentifier::ADR008, ADRMappedObject::MultiAgentRequest, "MultiAgentRequest", "agent_request_payload", "evaluate_multi_agent_request_field_boundary", "multi-agent request must not wrap agent-request payloads"},
        {ADRIdentifier::ADR008, ADRMappedObject::MultiAgentResult, "MultiAgentResult", "agent_result", "evaluate_multi_agent_result_field_boundary", "multi-agent result must not replace AgentResult"},
        {ADRIdentifier::ADR008, ADRMappedObject::MultiAgentResult, "MultiAgentResult", "final_agent_response", "evaluate_multi_agent_result_field_boundary", "multi-agent result must not become final user response"},
        {ADRIdentifier::ADR008, ADRMappedObject::WorkerTask, "WorkerTask", "global_session_state", "evaluate_worker_task_field_boundary", "worker task must not carry global session state"},
        {ADRIdentifier::ADR008, ADRMappedObject::WorkerTask, "WorkerTask", "global_fsm_state", "evaluate_worker_task_field_boundary", "worker task must not carry global FSM state"},
        {ADRIdentifier::ADR008, ADRMappedObject::WorkerTask, "WorkerTask", "session_fsm_state", "evaluate_worker_task_field_boundary", "worker task must not carry session FSM state"},
        {ADRIdentifier::ADR008, ADRMappedObject::WorkerLease, "WorkerLease", "global_session_state", "validate_worker_lease_forbidden_field", "worker lease must not carry global session state"},
        {ADRIdentifier::ADR008, ADRMappedObject::WorkerLease, "WorkerLease", "global_fsm_state", "validate_worker_lease_forbidden_field", "worker lease must not carry global FSM state"},
        {ADRIdentifier::ADR008, ADRMappedObject::WorkerLease, "WorkerLease", "session_fsm_state", "validate_worker_lease_forbidden_field", "worker lease must not carry session FSM state"},
        {ADRIdentifier::ADR008, ADRMappedObject::WorkerLease, "WorkerLease", "session_id", "validate_worker_lease_forbidden_field", "worker lease must not carry session identifiers from top-level control"},
        {ADRIdentifier::ADR008, ADRMappedObject::WorkerLease, "WorkerLease", "checkpoint_ref", "validate_worker_lease_forbidden_field", "worker lease must not become checkpoint entry"},
        {ADRIdentifier::ADR008, ADRMappedObject::WorkerLease, "WorkerLease", "resume_token", "validate_worker_lease_forbidden_field", "worker lease must not become resume entry"},
        {ADRIdentifier::ADR008, ADRMappedObject::WorkerLease, "WorkerLease", "agent_result", "validate_worker_lease_forbidden_field", "worker lease must not carry agent result semantics"},
        {ADRIdentifier::ADR008, ADRMappedObject::WorkerLease, "WorkerLease", "final_agent_response", "validate_worker_lease_forbidden_field", "worker lease must not carry final response semantics"},
        {ADRIdentifier::ADR008, ADRMappedObject::WorkerLease, "WorkerLease", "merged_result", "validate_worker_lease_forbidden_field", "worker lease must not carry merged-result semantics"},
    }};

constexpr std::string_view adr_identifier_name(ADRIdentifier adr) {
  switch (adr) {
    case ADRIdentifier::ADR006:
      return "ADR-006";
    case ADRIdentifier::ADR007:
      return "ADR-007";
    case ADRIdentifier::ADR008:
      return "ADR-008";
  }

  return "UnknownADR";
}

constexpr std::string_view adr_mapped_object_name(ADRMappedObject object) {
  switch (object) {
    case ADRMappedObject::ContextPacket:
      return "ContextPacket";
    case ADRMappedObject::PromptComposeRequest:
      return "PromptComposeRequest";
    case ADRMappedObject::PromptComposeResult:
      return "PromptComposeResult";
    case ADRMappedObject::ReflectionDecision:
      return "ReflectionDecision";
    case ADRMappedObject::RecoveryRequest:
      return "RecoveryRequest";
    case ADRMappedObject::RecoveryOutcome:
      return "RecoveryOutcome";
    case ADRMappedObject::MultiAgentRequest:
      return "MultiAgentRequest";
    case ADRMappedObject::MultiAgentResult:
      return "MultiAgentResult";
    case ADRMappedObject::WorkerTask:
      return "WorkerTask";
    case ADRMappedObject::WorkerLease:
      return "WorkerLease";
  }

  return "UnknownADRMappedObject";
}

constexpr std::size_t count_object_mappings_for_adr(ADRIdentifier adr) {
  std::size_t count = 0;
  for (const auto& entry : kADRFieldObjectMappingCatalog) {
    if (entry.adr == adr) {
      ++count;
    }
  }
  return count;
}

constexpr std::size_t count_forbidden_field_mappings_for_adr(ADRIdentifier adr) {
  std::size_t count = 0;
  for (const auto& entry : kADRForbiddenFieldMappingCatalog) {
    if (entry.adr == adr) {
      ++count;
    }
  }
  return count;
}

constexpr bool is_adr_object_mapped(ADRIdentifier adr, ADRMappedObject object) {
  for (const auto& entry : kADRFieldObjectMappingCatalog) {
    if (entry.adr == adr && entry.object == object) {
      return true;
    }
  }
  return false;
}

constexpr bool has_adr_forbidden_field_mapping(
    ADRIdentifier adr,
    ADRMappedObject object,
    std::string_view field_name) {
  for (const auto& entry : kADRForbiddenFieldMappingCatalog) {
    if (entry.adr == adr && entry.object == object &&
        entry.field_name == field_name) {
      return true;
    }
  }
  return false;
}

inline bool adr_forbidden_field_mapping_is_rejected(
    const ADRForbiddenFieldMappingEntry& entry) {
  switch (entry.object) {
    case ADRMappedObject::ContextPacket:
      return !evaluate_context_packet_prompt_field_boundary(entry.field_name)
                  .allowed;
    case ADRMappedObject::PromptComposeRequest:
      return !evaluate_compose_request_field_boundary(entry.field_name).allowed;
    case ADRMappedObject::PromptComposeResult:
      return !evaluate_compose_result_field_boundary(entry.field_name).allowed;
    case ADRMappedObject::ReflectionDecision:
      return !evaluate_reflection_decision_field_boundary(entry.field_name)
                  .allowed;
    case ADRMappedObject::RecoveryRequest:
      return !evaluate_recovery_request_field_boundary(entry.field_name).allowed;
    case ADRMappedObject::RecoveryOutcome:
      return !evaluate_recovery_outcome_field_boundary(entry.field_name).allowed;
    case ADRMappedObject::MultiAgentRequest:
      return !evaluate_multi_agent_request_field_boundary(entry.field_name)
                  .allowed;
    case ADRMappedObject::MultiAgentResult:
      return !evaluate_multi_agent_result_field_boundary(entry.field_name)
                  .allowed;
    case ADRMappedObject::WorkerTask:
      return !evaluate_worker_task_field_boundary(entry.field_name).allowed;
    case ADRMappedObject::WorkerLease:
      return !validate_worker_lease_forbidden_field(entry.field_name).ok;
  }

  return false;
}

inline ADRFieldMappingValidationResult validate_adr_field_mapping_catalog() {
  const bool object_counts_ok =
      count_object_mappings_for_adr(ADRIdentifier::ADR006) ==
          kADR006ObjectMappingCount &&
      count_object_mappings_for_adr(ADRIdentifier::ADR007) ==
          kADR007ObjectMappingCount &&
      count_object_mappings_for_adr(ADRIdentifier::ADR008) ==
          kADR008ObjectMappingCount;

  if (!object_counts_ok) {
    return ADRFieldMappingValidationResult{
        .ok = false,
        .object_catalog_complete = false,
        .forbidden_field_catalog_complete = false,
        .guard_dispatch_complete = false,
        .no_duplicate_entries = false,
        .first_failed_check = "object-counts",
        .reason = "object mapping catalog count does not match ADR wave expectations",
    };
  }

  const bool object_coverage_ok =
      is_adr_object_mapped(ADRIdentifier::ADR006,
                           ADRMappedObject::ContextPacket) &&
      is_adr_object_mapped(ADRIdentifier::ADR006,
                           ADRMappedObject::PromptComposeRequest) &&
      is_adr_object_mapped(ADRIdentifier::ADR006,
                           ADRMappedObject::PromptComposeResult) &&
      is_adr_object_mapped(ADRIdentifier::ADR007,
                           ADRMappedObject::ReflectionDecision) &&
      is_adr_object_mapped(ADRIdentifier::ADR007,
                           ADRMappedObject::RecoveryRequest) &&
      is_adr_object_mapped(ADRIdentifier::ADR007,
                           ADRMappedObject::RecoveryOutcome) &&
      is_adr_object_mapped(ADRIdentifier::ADR008,
                           ADRMappedObject::MultiAgentRequest) &&
      is_adr_object_mapped(ADRIdentifier::ADR008,
                           ADRMappedObject::MultiAgentResult) &&
      is_adr_object_mapped(ADRIdentifier::ADR008,
                           ADRMappedObject::WorkerTask) &&
      is_adr_object_mapped(ADRIdentifier::ADR008,
                           ADRMappedObject::WorkerLease);

  if (!object_coverage_ok) {
    return ADRFieldMappingValidationResult{
        .ok = false,
        .object_catalog_complete = false,
        .forbidden_field_catalog_complete = false,
        .guard_dispatch_complete = false,
        .no_duplicate_entries = false,
        .first_failed_check = "object-coverage",
        .reason = "object mapping catalog is missing one or more ADR-owned objects",
    };
  }

  const bool forbidden_counts_ok =
      count_forbidden_field_mappings_for_adr(ADRIdentifier::ADR006) ==
          kADR006ForbiddenFieldMappingCount &&
      count_forbidden_field_mappings_for_adr(ADRIdentifier::ADR007) ==
          kADR007ForbiddenFieldMappingCount &&
      count_forbidden_field_mappings_for_adr(ADRIdentifier::ADR008) ==
          kADR008ForbiddenFieldMappingCount;

  if (!forbidden_counts_ok) {
    return ADRFieldMappingValidationResult{
        .ok = false,
        .object_catalog_complete = true,
        .forbidden_field_catalog_complete = false,
        .guard_dispatch_complete = false,
        .no_duplicate_entries = false,
        .first_failed_check = "forbidden-field-counts",
        .reason = "forbidden-field mapping catalog count does not match ADR expectations",
    };
  }

  for (const auto& entry : kADRFieldObjectMappingCatalog) {
    if (entry.object_name.empty() || entry.primary_guard_symbol.empty() ||
        entry.boundary_guard_symbol.empty()) {
      return ADRFieldMappingValidationResult{
          .ok = false,
          .object_catalog_complete = false,
          .forbidden_field_catalog_complete = true,
          .guard_dispatch_complete = false,
          .no_duplicate_entries = false,
          .first_failed_check = "object-catalog-symbols",
          .reason = "object mapping catalog contains empty object or guard symbols",
      };
    }
  }

  for (std::size_t index = 0; index < kADRForbiddenFieldMappingCatalog.size();
       ++index) {
    const auto& entry = kADRForbiddenFieldMappingCatalog[index];
    if (entry.object_name.empty() || entry.field_name.empty() ||
        entry.guard_symbol.empty()) {
      return ADRFieldMappingValidationResult{
          .ok = false,
          .object_catalog_complete = true,
          .forbidden_field_catalog_complete = false,
          .guard_dispatch_complete = false,
          .no_duplicate_entries = false,
          .first_failed_check = "forbidden-field-symbols",
          .reason = "forbidden-field mapping catalog contains empty fields or guard symbols",
      };
    }

    if (!adr_forbidden_field_mapping_is_rejected(entry)) {
      return ADRFieldMappingValidationResult{
          .ok = false,
          .object_catalog_complete = true,
          .forbidden_field_catalog_complete = true,
          .guard_dispatch_complete = false,
          .no_duplicate_entries = false,
          .first_failed_check = "guard-dispatch",
          .reason = "mapped forbidden field is not rejected by its linked guard",
      };
    }

    for (std::size_t probe = index + 1;
         probe < kADRForbiddenFieldMappingCatalog.size(); ++probe) {
      const auto& other = kADRForbiddenFieldMappingCatalog[probe];
      if (entry.adr == other.adr && entry.object == other.object &&
          entry.field_name == other.field_name) {
        return ADRFieldMappingValidationResult{
            .ok = false,
            .object_catalog_complete = true,
            .forbidden_field_catalog_complete = true,
            .guard_dispatch_complete = true,
            .no_duplicate_entries = false,
            .first_failed_check = "duplicate-entry",
            .reason = "forbidden-field mapping catalog contains duplicate entries",
        };
      }
    }
  }

  return ADRFieldMappingValidationResult{
      .ok = true,
      .object_catalog_complete = true,
      .forbidden_field_catalog_complete = true,
      .guard_dispatch_complete = true,
      .no_duplicate_entries = true,
      .first_failed_check = "none",
      .reason = "ADR field mapping catalog passed completeness and guard-dispatch validation",
  };
}

}  // namespace dasall::contracts