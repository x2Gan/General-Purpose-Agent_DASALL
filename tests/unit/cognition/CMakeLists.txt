set(DASALL_COGNITION_UNIT_TEST_EXECUTABLE_TARGETS)

function(dasall_add_cognition_unit_test target_name source_file test_name)
	add_executable(${target_name}
		${source_file}
	)

	target_link_libraries(${target_name}
		PRIVATE
			dasall_cognition
			dasall_test_support
	)

	dasall_apply_common_options(${target_name})

	add_test(NAME ${test_name} COMMAND ${target_name})
	set_tests_properties(${test_name} PROPERTIES LABELS "unit;cognition")

	list(APPEND DASALL_COGNITION_UNIT_TEST_EXECUTABLE_TARGETS ${target_name})
	set(DASALL_COGNITION_UNIT_TEST_EXECUTABLE_TARGETS
		${DASALL_COGNITION_UNIT_TEST_EXECUTABLE_TARGETS}
		PARENT_SCOPE
	)
endfunction()

dasall_add_cognition_unit_test(
	dasall_cognition_interface_surface_unit_test
	CognitionInterfaceSurfaceTest.cpp
	CognitionInterfaceSurfaceTest
)

dasall_add_cognition_unit_test(
	dasall_cognition_config_projection_unit_test
	CognitionConfigProjectionTest.cpp
	CognitionConfigProjectionTest
)

dasall_add_cognition_unit_test(
	dasall_stage_policy_resolver_unit_test
	StagePolicyResolverTest.cpp
	StagePolicyResolverTest
)

dasall_add_cognition_unit_test(
	dasall_stage_policy_resolver_profile_diff_unit_test
	StagePolicyResolverProfileDiffTest.cpp
	StagePolicyResolverProfileDiffTest
)

dasall_add_cognition_unit_test(
	dasall_budget_aware_decision_unit_test
	BudgetAwareDecisionTest.cpp
	BudgetAwareDecisionTest
)

dasall_add_cognition_unit_test(
	dasall_perception_boundary_validation_unit_test
	PerceptionBoundaryValidationTest.cpp
	PerceptionBoundaryValidationTest
)

dasall_add_cognition_unit_test(
	dasall_cognition_facade_invalid_input_unit_test
	CognitionFacadeInvalidInputTest.cpp
	CognitionFacadeInvalidInputTest
)

dasall_add_cognition_unit_test(
	dasall_perception_engine_unit_test
	PerceptionEngineTest.cpp
	PerceptionEngineTest
)

dasall_add_cognition_unit_test(
	dasall_perception_clarification_rule_unit_test
	PerceptionClarificationRuleTest.cpp
	PerceptionClarificationRuleTest
)

dasall_add_cognition_unit_test(
	dasall_planner_plan_graph_unit_test
	PlannerPlanGraphTest.cpp
	PlannerPlanGraphTest
)

dasall_add_cognition_unit_test(
	dasall_planner_replan_unit_test
	PlannerReplanTest.cpp
	PlannerReplanTest
)

dasall_add_cognition_unit_test(
	dasall_planner_node_budget_unit_test
	PlannerNodeBudgetTest.cpp
	PlannerNodeBudgetTest
)

dasall_add_cognition_unit_test(
	dasall_reasoner_action_decision_unit_test
	ReasonerActionDecisionTest.cpp
	ReasonerActionDecisionTest
)

dasall_add_cognition_unit_test(
	dasall_reasoner_clarification_threshold_unit_test
	ReasonerClarificationThresholdTest.cpp
	ReasonerClarificationThresholdTest
)

dasall_add_cognition_unit_test(
	dasall_reasoner_conflict_resolution_unit_test
	ReasonerConflictResolutionTest.cpp
	ReasonerConflictResolutionTest
)

dasall_add_cognition_unit_test(
	dasall_reflection_engine_decision_unit_test
	ReflectionEngineDecisionTest.cpp
	ReflectionEngineDecisionTest
)

dasall_add_cognition_unit_test(
	dasall_reflection_engine_belief_invalidation_unit_test
	ReflectionEngineBeliefInvalidationTest.cpp
	ReflectionEngineBeliefInvalidationTest
)

dasall_add_cognition_unit_test(
	dasall_reflection_engine_conservative_abort_unit_test
	ReflectionEngineConservativeAbortTest.cpp
	ReflectionEngineConservativeAbortTest
)

dasall_add_cognition_unit_test(
	dasall_belief_update_synthesizer_unit_test
	BeliefUpdateSynthesizerTest.cpp
	BeliefUpdateSynthesizerTest
)

dasall_add_cognition_unit_test(
	dasall_belief_update_merge_mode_unit_test
	BeliefUpdateMergeModeTest.cpp
	BeliefUpdateMergeModeTest
)

dasall_add_cognition_unit_test(
	dasall_belief_update_evidence_dedup_unit_test
	BeliefUpdateEvidenceDedupTest.cpp
	BeliefUpdateEvidenceDedupTest
)

dasall_add_cognition_unit_test(
	dasall_response_builder_agent_result_mapping_unit_test
	ResponseBuilderAgentResultMappingTest.cpp
	ResponseBuilderAgentResultMappingTest
)

dasall_add_cognition_unit_test(
	dasall_response_builder_template_fallback_unit_test
	ResponseBuilderTemplateFallbackTest.cpp
	ResponseBuilderTemplateFallbackTest
)

dasall_add_cognition_unit_test(
	dasall_response_builder_redaction_unit_test
	ResponseBuilderRedactionTest.cpp
	ResponseBuilderRedactionTest
)

dasall_add_cognition_unit_test(
	dasall_mock_cognition_fixture_surface_unit_test
	MockCognitionFixtureSurfaceTest.cpp
	MockCognitionFixtureSurfaceTest
)

dasall_add_cognition_unit_test(
	dasall_cognition_llm_bridge_projection_unit_test
	CognitionLlmBridgeProjectionTest.cpp
	CognitionLlmBridgeProjectionTest
)

dasall_add_cognition_unit_test(
	dasall_cognition_llm_bridge_error_mapping_unit_test
	CognitionLlmBridgeErrorMappingTest.cpp
	CognitionLlmBridgeErrorMappingTest
)

dasall_add_cognition_unit_test(
	dasall_stage_model_hint_projection_unit_test
	StageModelHintProjectionTest.cpp
	StageModelHintProjectionTest
)

dasall_add_cognition_unit_test(
	dasall_stage_output_validator_schema_unit_test
	StageOutputValidatorSchemaTest.cpp
	StageOutputValidatorSchemaTest
)

dasall_add_cognition_unit_test(
	dasall_stage_output_validator_plan_graph_invariant_unit_test
	StageOutputValidatorPlanGraphInvariantTest.cpp
	StageOutputValidatorPlanGraphInvariantTest
)

dasall_add_cognition_unit_test(
	dasall_stage_output_validator_response_envelope_unit_test
	StageOutputValidatorResponseEnvelopeTest.cpp
	StageOutputValidatorResponseEnvelopeTest
)

dasall_add_cognition_unit_test(
	dasall_cognition_telemetry_fields_unit_test
	CognitionTelemetryFieldsTest.cpp
	CognitionTelemetryFieldsTest
)

dasall_add_cognition_unit_test(
	dasall_cognition_telemetry_redaction_unit_test
	CognitionTelemetryRedactionTest.cpp
	CognitionTelemetryRedactionTest
)

dasall_add_cognition_unit_test(
	dasall_cognition_telemetry_failure_isolation_unit_test
	CognitionTelemetryFailureIsolationTest.cpp
	CognitionTelemetryFailureIsolationTest
)

target_link_libraries(dasall_cognition_config_projection_unit_test
	PRIVATE
		dasall_profiles
)

target_include_directories(dasall_cognition_config_projection_unit_test
	PRIVATE
		${CMAKE_SOURCE_DIR}/cognition/src
)

target_link_libraries(dasall_stage_policy_resolver_unit_test
	PRIVATE
		dasall_profiles
)

target_link_libraries(dasall_stage_policy_resolver_profile_diff_unit_test
	PRIVATE
		dasall_profiles
)

target_link_libraries(dasall_budget_aware_decision_unit_test
	PRIVATE
		dasall_profiles
)

target_include_directories(dasall_stage_policy_resolver_unit_test
	PRIVATE
		${CMAKE_SOURCE_DIR}/cognition/src
)

target_include_directories(dasall_stage_policy_resolver_profile_diff_unit_test
	PRIVATE
		${CMAKE_SOURCE_DIR}/cognition/src
)

target_include_directories(dasall_budget_aware_decision_unit_test
	PRIVATE
		${CMAKE_SOURCE_DIR}/cognition/src
)

target_include_directories(dasall_perception_boundary_validation_unit_test
	PRIVATE
		${CMAKE_SOURCE_DIR}/cognition/src
)

target_include_directories(dasall_mock_cognition_fixture_surface_unit_test
	PRIVATE
		${CMAKE_SOURCE_DIR}/cognition/src
)

target_include_directories(dasall_cognition_llm_bridge_projection_unit_test
	PRIVATE
		${CMAKE_SOURCE_DIR}/cognition/src
)

target_include_directories(dasall_cognition_llm_bridge_error_mapping_unit_test
	PRIVATE
		${CMAKE_SOURCE_DIR}/cognition/src
)

target_include_directories(dasall_stage_model_hint_projection_unit_test
	PRIVATE
		${CMAKE_SOURCE_DIR}/cognition/src
)

target_include_directories(dasall_stage_output_validator_schema_unit_test
	PRIVATE
		${CMAKE_SOURCE_DIR}/cognition/src
)

target_include_directories(dasall_stage_output_validator_plan_graph_invariant_unit_test
	PRIVATE
		${CMAKE_SOURCE_DIR}/cognition/src
)

target_include_directories(dasall_stage_output_validator_response_envelope_unit_test
	PRIVATE
		${CMAKE_SOURCE_DIR}/cognition/src
)

target_include_directories(dasall_cognition_telemetry_fields_unit_test
	PRIVATE
		${CMAKE_SOURCE_DIR}/cognition/src
)

target_include_directories(dasall_cognition_telemetry_redaction_unit_test
	PRIVATE
		${CMAKE_SOURCE_DIR}/cognition/src
)

target_include_directories(dasall_cognition_telemetry_failure_isolation_unit_test
	PRIVATE
		${CMAKE_SOURCE_DIR}/cognition/src
)

target_link_libraries(dasall_cognition_llm_bridge_projection_unit_test
	PRIVATE
		dasall_llm
)

target_link_libraries(dasall_cognition_llm_bridge_error_mapping_unit_test
	PRIVATE
		dasall_llm
)

target_link_libraries(dasall_stage_model_hint_projection_unit_test
	PRIVATE
		dasall_llm
)

target_link_libraries(dasall_stage_output_validator_schema_unit_test
	PRIVATE
		dasall_llm
)

target_link_libraries(dasall_stage_output_validator_plan_graph_invariant_unit_test
	PRIVATE
		dasall_llm
)

target_link_libraries(dasall_stage_output_validator_response_envelope_unit_test
	PRIVATE
		dasall_llm
)

target_include_directories(dasall_perception_engine_unit_test
	PRIVATE
		${CMAKE_SOURCE_DIR}/cognition/src
)

target_include_directories(dasall_perception_clarification_rule_unit_test
	PRIVATE
		${CMAKE_SOURCE_DIR}/cognition/src
)

target_include_directories(dasall_planner_plan_graph_unit_test
	PRIVATE
		${CMAKE_SOURCE_DIR}/cognition/src
)

target_include_directories(dasall_planner_replan_unit_test
	PRIVATE
		${CMAKE_SOURCE_DIR}/cognition/src
)

target_include_directories(dasall_planner_node_budget_unit_test
	PRIVATE
		${CMAKE_SOURCE_DIR}/cognition/src
)

target_include_directories(dasall_reasoner_action_decision_unit_test
	PRIVATE
		${CMAKE_SOURCE_DIR}/cognition/src
)

target_include_directories(dasall_reasoner_clarification_threshold_unit_test
	PRIVATE
		${CMAKE_SOURCE_DIR}/cognition/src
)

target_include_directories(dasall_reasoner_conflict_resolution_unit_test
	PRIVATE
		${CMAKE_SOURCE_DIR}/cognition/src
)

target_include_directories(dasall_reflection_engine_decision_unit_test
	PRIVATE
		${CMAKE_SOURCE_DIR}/cognition/src
)

target_include_directories(dasall_reflection_engine_belief_invalidation_unit_test
	PRIVATE
		${CMAKE_SOURCE_DIR}/cognition/src
)

target_include_directories(dasall_reflection_engine_conservative_abort_unit_test
	PRIVATE
		${CMAKE_SOURCE_DIR}/cognition/src
)

target_include_directories(dasall_belief_update_synthesizer_unit_test
	PRIVATE
		${CMAKE_SOURCE_DIR}/cognition/src
)

target_include_directories(dasall_belief_update_merge_mode_unit_test
	PRIVATE
		${CMAKE_SOURCE_DIR}/cognition/src
)

target_include_directories(dasall_belief_update_evidence_dedup_unit_test
	PRIVATE
		${CMAKE_SOURCE_DIR}/cognition/src
)

if(NOT DASALL_COGNITION_UNIT_TEST_EXECUTABLE_TARGETS)
	message(FATAL_ERROR "cognition unit registration requires at least one test target")
endif()

set(DASALL_COGNITION_UNIT_TEST_EXECUTABLE_TARGETS
	${DASALL_COGNITION_UNIT_TEST_EXECUTABLE_TARGETS}
	PARENT_SCOPE
)
