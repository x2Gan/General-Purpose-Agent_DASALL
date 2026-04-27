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

if(NOT DASALL_COGNITION_UNIT_TEST_EXECUTABLE_TARGETS)
	message(FATAL_ERROR "cognition unit registration requires at least one test target")
endif()

set(DASALL_COGNITION_UNIT_TEST_EXECUTABLE_TARGETS
	${DASALL_COGNITION_UNIT_TEST_EXECUTABLE_TARGETS}
	PARENT_SCOPE
)
