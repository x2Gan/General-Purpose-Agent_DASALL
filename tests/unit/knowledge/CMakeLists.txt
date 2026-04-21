set(DASALL_KNOWLEDGE_UNIT_TEST_EXECUTABLE_TARGETS)

macro(dasall_add_knowledge_unit_test target_name source_file test_name)
	add_executable(${target_name}
		${CMAKE_CURRENT_SOURCE_DIR}/${source_file}
	)

	target_link_libraries(${target_name}
		PRIVATE
			dasall_knowledge
			dasall_test_support
	)

	dasall_apply_common_options(${target_name})

	add_test(NAME ${test_name} COMMAND ${target_name})
	set_tests_properties(${test_name} PROPERTIES LABELS "unit;knowledge")

	list(APPEND DASALL_KNOWLEDGE_UNIT_TEST_EXECUTABLE_TARGETS ${target_name})
endmacro()

dasall_add_knowledge_unit_test(
	dasall_knowledge_interface_surface_unit_test
	KnowledgeInterfaceSurfaceSkeletonTest.cpp
	dasall_knowledge_interface_surface_unit_test
)

dasall_add_knowledge_unit_test(
	dasall_knowledge_config_projection_unit_test
	KnowledgeConfigProjectionTest.cpp
	KnowledgeConfigProjectionTest
)

dasall_add_knowledge_unit_test(
	dasall_knowledge_telemetry_unit_test
	KnowledgeTelemetryTest.cpp
	KnowledgeTelemetryTest
)

dasall_add_knowledge_unit_test(
	dasall_knowledge_observability_fields_unit_test
	KnowledgeTelemetryFieldSetTest.cpp
	KnowledgeTelemetryFieldSetTest
)

dasall_add_knowledge_unit_test(
	dasall_knowledge_telemetry_degrade_event_unit_test
	KnowledgeTelemetryDegradeEventTest.cpp
	KnowledgeTelemetryDegradeEventTest
)

target_link_libraries(dasall_knowledge_config_projection_unit_test
	PRIVATE
		dasall_profiles
)

target_include_directories(dasall_knowledge_config_projection_unit_test
	PRIVATE
		${CMAKE_SOURCE_DIR}/knowledge/src
)

if(NOT DASALL_KNOWLEDGE_UNIT_TEST_EXECUTABLE_TARGETS)
	message(FATAL_ERROR "knowledge unit registration requires at least one test target")
endif()

set(DASALL_KNOWLEDGE_UNIT_TEST_EXECUTABLE_TARGETS
	${DASALL_KNOWLEDGE_UNIT_TEST_EXECUTABLE_TARGETS}
	PARENT_SCOPE
)
