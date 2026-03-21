set(DASALL_CONTRACT_TEST_EXECUTABLE_TARGETS "")

# Centralized registration keeps contract test entrypoints converged in a
# single CMake path and guarantees each registered test is labeled as contract
# plus one explicit group label: smoke/error/checkpoint/event.
function(dasall_register_contract_test executable_target_name ctest_name source_file group_label)
  add_executable(${executable_target_name}
    ${source_file}
  )

  target_link_libraries(${executable_target_name}
    PRIVATE
      dasall_test_support
  )

  dasall_apply_common_options(${executable_target_name})

  add_test(NAME ${ctest_name} COMMAND ${executable_target_name})
  set_tests_properties(${ctest_name} PROPERTIES LABELS "contract;${group_label}")

  list(APPEND DASALL_CONTRACT_TEST_EXECUTABLE_TARGETS ${executable_target_name})
  set(DASALL_CONTRACT_TEST_EXECUTABLE_TARGETS "${DASALL_CONTRACT_TEST_EXECUTABLE_TARGETS}" PARENT_SCOPE)
endfunction()

# smoke group
dasall_register_contract_test(
  dasall_contract_smoke_test
  dasall_contract_smoke_test
  smoke/ContractSmokeTest.cpp
  smoke
)

dasall_register_contract_test(
  dasall_contract_compatibility_test
  CompatibilityContractTest
  smoke/CompatibilityContractTest.cpp
  smoke
)

dasall_register_contract_test(
  dasall_contract_object_boundary_catalog_test
  dasall_contract_object_boundary_catalog_test
  smoke/ObjectBoundaryCatalogContractTest.cpp
  smoke
)

dasall_register_contract_test(
  dasall_contract_stable_type_presence_test
  StableTypePresenceContractTest
  smoke/StableTypePresenceContractTest.cpp
  smoke
)

dasall_register_contract_test(
  dasall_contract_boundary_guards_test
  BoundaryGuardsContractTest
  smoke/BoundaryGuardsContractTest.cpp
  smoke
)

dasall_register_contract_test(
  dasall_contract_context_packet_boundary_test
  ContextPacketBoundaryContractTest
  smoke/ContextPacketBoundaryContractTest.cpp
  smoke
)

dasall_register_contract_test(
  dasall_contract_recovery_boundary_test
  RecoveryBoundaryContractTest
  smoke/RecoveryBoundaryContractTest.cpp
  smoke
)

dasall_register_contract_test(
  dasall_contract_multi_agent_boundary_test
  MultiAgentBoundaryContractTest
  smoke/MultiAgentBoundaryContractTest.cpp
  smoke
)

dasall_register_contract_test(
  dasall_contract_cross_cutting_contracts_smoke_test
  CrossCuttingContractsSmokeTest
  smoke/CrossCuttingContractsSmokeTest.cpp
  smoke
)

dasall_register_contract_test(
  dasall_contract_field_evolution_guards_test
  FieldEvolutionGuardsContractTest
  smoke/FieldEvolutionGuardsContractTest.cpp
  smoke
)

dasall_register_contract_test(
  dasall_contract_identity_metadata_test
  IdentityMetadataContractTest
  smoke/IdentityMetadataContractTest.cpp
  smoke
)

dasall_register_contract_test(
  dasall_contract_time_deadline_test
  TimeDeadlineContractTest
  smoke/TimeDeadlineContractTest.cpp
  smoke
)

dasall_register_contract_test(
  dasall_contract_enum_lifecycle_test
  EnumLifecycleContractTest
  smoke/EnumLifecycleContractTest.cpp
  smoke
)

dasall_register_contract_test(
  dasall_contract_m2_checklist_test
  M2ChecklistContractTest
  smoke/M2ChecklistContractTest.cpp
  smoke
)

dasall_register_contract_test(
  dasall_contract_main_flow_contracts_smoke_test
  MainFlowContractsSmokeTest
  smoke/MainFlowContractsSmokeTest.cpp
  smoke
)

dasall_register_contract_test(
  dasall_contract_adr_field_mapping_test
  ADRFieldMappingContractTest
  smoke/ADRFieldMappingContractTest.cpp
  smoke
)

dasall_register_contract_test(
  dasall_contract_m4_checklist_test
  M4ChecklistContractTest
  smoke/M4ChecklistContractTest.cpp
  smoke
)

# smoke group (WP05-T001-B): domain rollout guard smoke test.
# Validates the approved WP05 rollout waves, including allowed in-wave
# parallelism and rejection of missing prerequisites or duplicate starts.
dasall_register_contract_test(
  dasall_contract_domain_rollout_test
  DomainRolloutContractTest
  smoke/DomainRolloutContractTest.cpp
  smoke
)

# smoke group (WP05-T011-B): interface-candidate catalog smoke test.
# Validates that only retained cross-module stable dependency surfaces enter
# the catalog and that review-ready candidates remain queryable.
dasall_register_contract_test(
  dasall_contract_interface_catalog_test
  InterfaceCatalogContractTest
  smoke/InterfaceCatalogContractTest.cpp
  smoke
)

# smoke group (WP05-T012-B): interface-admission guard smoke test.
# Validates admit/postpone/return decisions over the stage-5 interface catalog
# without expanding into real interface signature design.
dasall_register_contract_test(
  dasall_contract_interface_admission_test
  InterfaceAdmissionContractTest
  smoke/InterfaceAdmissionContractTest.cpp
  smoke
)

# serialization group (WP05-T013-B): serialization compatibility contract test.
# Covers round-trip stability, forward unknown-field tolerance, legacy timeout
# migration, and EventEnvelope header whitelist boundary checks.
dasall_register_contract_test(
  dasall_contract_serialization_compatibility_test
  SerializationCompatibilityContractTest
  serialization/SerializationCompatibilityContractTest.cpp
  serialization
)

# tool group (WP05-T002-B)
dasall_register_contract_test(
  dasall_contract_tool_request_test
  ToolRequestContractTest
  tool/ToolRequestContractTest.cpp
  tool
)

# tool group (WP05-T003-B)
dasall_register_contract_test(
  dasall_contract_tool_result_test
  ToolResultContractTest
  tool/ToolResultContractTest.cpp
  tool
)

# tool group (WP05-T004-B)
dasall_register_contract_test(
  dasall_contract_tool_descriptor_ir_test
  ToolDescriptorIRContractTest
  tool/ToolDescriptorIRContractTest.cpp
  tool
)

# llm group (WP05-T010-B)
dasall_register_contract_test(
  dasall_contract_llm_request_response_test
  LLMRequestResponseContractTest
  llm/LLMRequestResponseContractTest.cpp
  llm
)

# memory group (WP05-T006-B)
dasall_register_contract_test(
  dasall_contract_turn_session_summary_memory_test
  TurnSessionSummaryMemoryContractTest
  memory/TurnSessionSummaryMemoryContractTest.cpp
  memory
)

# memory group (WP05-T007-B)
dasall_register_contract_test(
  dasall_contract_memory_fact_experience_test
  MemoryFactExperienceContractTest
  memory/MemoryFactExperienceContractTest.cpp
  memory
)

# error group
dasall_register_contract_test(
  dasall_contract_result_code_test
  ResultCodeContractTest
  error/ResultCodeContractTest.cpp
  error
)

dasall_register_contract_test(
  dasall_contract_error_info_test
  ErrorInfoContractTest
  error/ErrorInfoContractTest.cpp
  error
)

dasall_register_contract_test(
  dasall_contract_error_source_test
  ErrorSourceContractTest
  error/ErrorSourceContractTest.cpp
  error
)

# checkpoint group
dasall_register_contract_test(
  dasall_contract_runtime_budget_test
  RuntimeBudgetContractTest
  checkpoint/RuntimeBudgetContractTest.cpp
  checkpoint
)

dasall_register_contract_test(
  dasall_contract_budget_snapshot_test
  BudgetSnapshotContractTest
  checkpoint/BudgetSnapshotContractTest.cpp
  checkpoint
)

# event group
dasall_register_contract_test(
  dasall_contract_event_envelope_test
  EventEnvelopeContractTest
  event/EventEnvelopeContractTest.cpp
  event
)

# event group (WP05-T009-B)
dasall_register_contract_test(
  dasall_contract_event_type_payload_test
  EventTypePayloadContractTest
  event/EventTypePayloadContractTest.cpp
  event
)

# agent group (WP03-T002-B)
dasall_register_contract_test(
  dasall_contract_agent_request_test
  AgentRequestContractTest
  agent/AgentRequestContractTest.cpp
  agent
)

# agent group (WP03-T003-B)
dasall_register_contract_test(
  dasall_contract_agent_request_field_test
  AgentRequestFieldContractTest
  agent/AgentRequestFieldContractTest.cpp
  agent
)

# agent group (WP03-T004-B)
dasall_register_contract_test(
  dasall_contract_goal_contract_test
  GoalContractContractTest
  agent/GoalContractContractTest.cpp
  agent
)

# agent group (WP03-T005-B)
dasall_register_contract_test(
  dasall_contract_goal_contract_field_test
  GoalContractFieldContractTest
  agent/GoalContractFieldContractTest.cpp
  agent
)

# observation group (WP03-T006-B)
dasall_register_contract_test(
  dasall_contract_observation_test
  ObservationContractTest
  observation/ObservationContractTest.cpp
  observation
)

# observation group (WP03-T007-B)
dasall_register_contract_test(
  dasall_contract_observation_source_test
  ObservationSourceContractTest
  observation/ObservationSourceContractTest.cpp
  observation
)

# observation group (WP03-T008-B)
dasall_register_contract_test(
  dasall_contract_observation_digest_boundary_test
  ObservationDigestBoundaryContractTest
  observation/ObservationDigestBoundaryContractTest.cpp
  observation
)

# agent group (WP03-T009-B)
dasall_register_contract_test(
  dasall_contract_belief_state_test
  BeliefStateContractTest
  agent/BeliefStateContractTest.cpp
  agent
)

# agent group (WP04-T014-B)
dasall_register_contract_test(
  dasall_contract_multi_agent_request_test
  MultiAgentRequestContractTest
  agent/MultiAgentRequestContractTest.cpp
  agent
)

# agent group (WP04-T015-B)
dasall_register_contract_test(
  dasall_contract_multi_agent_request_field_test
  MultiAgentRequestFieldContractTest
  agent/MultiAgentRequestFieldContractTest.cpp
  agent
)

# agent group (WP04-T016-B)
dasall_register_contract_test(
  dasall_contract_multi_agent_result_test
  MultiAgentResultContractTest
  agent/MultiAgentResultContractTest.cpp
  agent
)

# agent group (WP04-T017-B)
dasall_register_contract_test(
  dasall_contract_multi_agent_result_field_test
  MultiAgentResultFieldContractTest
  agent/MultiAgentResultFieldContractTest.cpp
  agent
)

# task group (WP04-T018-B)
dasall_register_contract_test(
  dasall_contract_worker_task_test
  WorkerTaskContractTest
  task/WorkerTaskContractTest.cpp
  task
)

# task group (WP04-T019-B)
dasall_register_contract_test(
  dasall_contract_worker_task_field_test
  WorkerTaskFieldContractTest
  task/WorkerTaskFieldContractTest.cpp
  task
)

# task group (WP04-T020-B)
dasall_register_contract_test(
  dasall_contract_worker_lease_test
  WorkerLeaseContractTest
  task/WorkerLeaseContractTest.cpp
  task
)

# task group (WP04-T021-B)
dasall_register_contract_test(
  dasall_contract_worker_lease_field_test
  WorkerLeaseFieldContractTest
  task/WorkerLeaseFieldContractTest.cpp
  task
)

# task group (WP05-T008-B)
dasall_register_contract_test(
  dasall_contract_task_domain_test
  TaskDomainContractTest
  task/TaskDomainContractTest.cpp
  task
)

# context group (WP03-T010-B)
dasall_register_contract_test(
  dasall_contract_context_packet_main_flow_test
  ContextPacketMainFlowContractTest
  context/ContextPacketMainFlowContractTest.cpp
  context
)

# context group (WP03-T011-B)
dasall_register_contract_test(
  dasall_contract_context_packet_field_test
  ContextPacketFieldContractTest
  context/ContextPacketFieldContractTest.cpp
  context
)

# checkpoint group (WP03-T012-B)
dasall_register_contract_test(
  dasall_contract_checkpoint_test
  CheckpointContractTest
  checkpoint/CheckpointContractTest.cpp
  checkpoint
)

# checkpoint group (WP03-T013-B)
dasall_register_contract_test(
  dasall_contract_checkpoint_field_test
  CheckpointFieldContractTest
  checkpoint/CheckpointFieldContractTest.cpp
  checkpoint
)

# agent group (WP03-T014-B)
dasall_register_contract_test(
  dasall_contract_agent_result_test
  AgentResultContractTest
  agent/AgentResultContractTest.cpp
  agent
)

# e2e group (WP03-T015-B)
dasall_register_contract_test(
  dasall_contract_main_flow_e2e_test
  MainFlowContractE2ETest
  e2e/MainFlowContractE2ETest.cpp
  e2e
)

# smoke group (WP03-T016-B)
dasall_register_contract_test(
  dasall_contract_main_flow_overlap_test
  MainFlowOverlapContractTest
  smoke/MainFlowOverlapContractTest.cpp
  smoke
)

# smoke group (WP03-T017-B)
dasall_register_contract_test(
  dasall_contract_m3_checklist_test
  M3ChecklistContractTest
  smoke/M3ChecklistContractTest.cpp
  smoke
)

# smoke group (WP04-T001-B): Prompt boundary contracts aggregate entry smoke test.
# Validates ADR-006 §6.1/§6.2/§6.3 boundary guards for ContextPacket,
# PromptComposeRequest, and PromptComposeResult.
dasall_register_contract_test(
  dasall_contract_prompt_boundary_contracts_smoke_test
  PromptBoundaryContractsSmokeTest
  smoke/PromptBoundaryContractsSmokeTest.cpp
  smoke
)

# prompt group (WP04-T002-B): PromptComposeRequest contract test.
# Validates 3-layer guards (L1 required / L2 boundary / L3 field rules) for
# PromptComposeRequest.h and PromptComposeRequestGuards.h (ADR-006 §6.2).
dasall_register_contract_test(
  dasall_contract_prompt_compose_request_test
  PromptComposeRequestContractTest
  prompt/PromptComposeRequestContractTest.cpp
  prompt
)

# prompt group (WP04-T003-B): PromptComposeRequest field contract test.
# Validates field-level rules and illegal combinations from the T003 field
# table, including request/context binding and visible_tools uniqueness.
dasall_register_contract_test(
  dasall_contract_prompt_compose_request_field_test
  PromptComposeRequestFieldContractTest
  prompt/PromptComposeRequestFieldContractTest.cpp
  prompt
)

# prompt group (WP04-T004-B): PromptComposeResult contract test.
# Validates required compose-result fields and rejects memory/context
# write-back semantics via the Prompt boundary catalog.
dasall_register_contract_test(
  dasall_contract_prompt_compose_result_test
  PromptComposeResultContractTest
  prompt/PromptComposeResultContractTest.cpp
  prompt
)

# prompt group (WP04-T005-B): PromptComposeResult field contract test.
# Validates optional metadata field rules, including pruned_sections audit-list
# uniqueness and composition warning vector hygiene.
dasall_register_contract_test(
  dasall_contract_prompt_compose_result_field_test
  PromptComposeResultFieldContractTest
  prompt/PromptComposeResultFieldContractTest.cpp
  prompt
)

# prompt group (WP05-T005-B): PromptSpec/PromptRelease contract test.
# Validates PromptSpec selection-surface constraints and PromptRelease
# versioned-asset constraints, including cross-layer field-boundary rejection.
dasall_register_contract_test(
  dasall_contract_prompt_spec_release_test
  PromptSpecReleaseContractTest
  prompt/PromptSpecReleaseContractTest.cpp
  prompt
)

# smoke group (WP04-T006-B): Recovery boundary contracts aggregate entry smoke
# test. Validates ADR-007 impact-object catalog plus aggregate visibility of the
# existing reflection/outcome boundary guards.
dasall_register_contract_test(
  dasall_contract_recovery_boundary_contracts_smoke_test
  RecoveryBoundaryContractsSmokeTest
  smoke/RecoveryBoundaryContractsSmokeTest.cpp
  smoke
)

# smoke group (WP04-T013-B): Multi-agent boundary contracts aggregate entry
# smoke test. Validates ADR-008 impact-object catalog plus aggregate visibility
# of the existing request/result/worker-task boundary guards.
dasall_register_contract_test(
  dasall_contract_multi_agent_boundary_contracts_smoke_test
  MultiAgentBoundaryContractsSmokeTest
  smoke/MultiAgentBoundaryContractsSmokeTest.cpp
  smoke
)

# checkpoint group (WP04-T007-B): ReflectionDecision contract test.
# Validates the suggestion-only ReflectionDecision object, its required and
# boundary guards, and the reused ADR-007 scheduling-field rejection gate.
dasall_register_contract_test(
  dasall_contract_reflection_decision_test
  ReflectionDecisionContractTest
  checkpoint/ReflectionDecisionContractTest.cpp
  checkpoint
)

# checkpoint group (WP04-T008-B): ReflectionDecision field contract test.
# Validates T008 field-table hygiene rules including finite confidence,
# observation-ref vector semantics, tags hygiene, and inherited ADR-007
# scheduling-field rejection.
dasall_register_contract_test(
  dasall_contract_reflection_decision_field_test
  ReflectionDecisionFieldContractTest
  checkpoint/ReflectionDecisionFieldContractTest.cpp
  checkpoint
)

# checkpoint group (WP04-T009-B): RecoveryRequest contract test.
# Validates the runtime-owned recovery-admission object, nested evidence reuse,
# and rejection of top-level reflection/outcome semantics.
dasall_register_contract_test(
  dasall_contract_recovery_request_test
  RecoveryRequestContractTest
  checkpoint/RecoveryRequestContractTest.cpp
  checkpoint
)

# checkpoint group (WP04-T010-B): RecoveryRequest field contract test.
# Validates T010 field-table rules including nested evidence-bundle
# consistency, source-ref alignment, and idempotency-key requirements when
# safe replay is asserted after observed side-effects.
dasall_register_contract_test(
  dasall_contract_recovery_request_field_test
  RecoveryRequestFieldContractTest
  checkpoint/RecoveryRequestFieldContractTest.cpp
  checkpoint
)

# checkpoint group (WP04-T011-B): RecoveryOutcome contract test.
# Validates the runtime-owned recovery-result object, its required/boundary
# guards, and the reused ADR-007 failure-attribution rejection gate.
dasall_register_contract_test(
  dasall_contract_recovery_outcome_test
  RecoveryOutcomeContractTest
  checkpoint/RecoveryOutcomeContractTest.cpp
  checkpoint
)

# checkpoint group (WP04-T012-B): RecoveryOutcome field contract test.
# Validates T012 field-table rules including whitespace-only string hygiene and
# distinct checkpoint/compensation control references.
dasall_register_contract_test(
  dasall_contract_recovery_outcome_field_test
  RecoveryOutcomeFieldContractTest
  checkpoint/RecoveryOutcomeFieldContractTest.cpp
  checkpoint
)

# Export the converged contract test target list to tests/CMakeLists.txt.
set(DASALL_CONTRACT_TEST_EXECUTABLE_TARGETS
  "${DASALL_CONTRACT_TEST_EXECUTABLE_TARGETS}"
  PARENT_SCOPE
)


