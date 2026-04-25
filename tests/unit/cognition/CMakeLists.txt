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

if(NOT DASALL_COGNITION_UNIT_TEST_EXECUTABLE_TARGETS)
	message(FATAL_ERROR "cognition unit registration requires at least one test target")
endif()

set(DASALL_COGNITION_UNIT_TEST_EXECUTABLE_TARGETS
	${DASALL_COGNITION_UNIT_TEST_EXECUTABLE_TARGETS}
	PARENT_SCOPE
)
