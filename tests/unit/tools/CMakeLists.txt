function(dasall_add_tools_surface_test target_name source_file test_name)
	add_executable(${target_name}
		${source_file}
	)

	target_link_libraries(${target_name}
		PRIVATE
			dasall_tools
	)

	dasall_apply_common_options(${target_name})

	add_test(NAME ${test_name} COMMAND ${target_name})
	set_tests_properties(${test_name} PROPERTIES LABELS "unit;tools")
endfunction()

function(dasall_add_tools_behavior_test target_name source_file test_name)
	add_executable(${target_name}
		${source_file}
	)

	target_include_directories(${target_name}
		PRIVATE
			${CMAKE_SOURCE_DIR}/tools/src
			${CMAKE_SOURCE_DIR}/profiles/include
	)

	target_link_libraries(${target_name}
		PRIVATE
			dasall_tools
			dasall_test_support
	)

	dasall_apply_common_options(${target_name})

	add_test(NAME ${test_name} COMMAND ${target_name})
	set_tests_properties(${test_name} PROPERTIES LABELS "unit;tools")
endfunction()

dasall_add_tools_surface_test(
	dasall_tool_invocation_context_surface_unit_test
	ToolInvocationContextSurfaceTest.cpp
	ToolInvocationContextSurfaceTest
)

dasall_add_tools_surface_test(
	dasall_tool_invocation_envelope_surface_unit_test
	ToolInvocationEnvelopeSurfaceTest.cpp
	ToolInvocationEnvelopeSurfaceTest
)

dasall_add_tools_surface_test(
	dasall_tool_interface_surface_unit_test
	ToolInterfaceSurfaceTest.cpp
	ToolInterfaceSurfaceTest
)

dasall_add_tools_surface_test(
	dasall_tool_policy_capability_surface_unit_test
	ToolPolicyCapabilitySurfaceTest.cpp
	ToolPolicyCapabilitySurfaceTest
)

dasall_add_tools_surface_test(
	dasall_mcp_interface_surface_unit_test
	MCPInterfaceSurfaceTest.cpp
	MCPInterfaceSurfaceTest
)

dasall_add_tools_surface_test(
	dasall_tool_plugin_provider_surface_unit_test
	ToolPluginProviderSurfaceTest.cpp
	ToolPluginProviderSurfaceTest
)

dasall_add_tools_behavior_test(
	dasall_tool_registry_unit_test
	ToolRegistryTest.cpp
	ToolRegistryTest
)

dasall_add_tools_behavior_test(
	dasall_tool_registry_concurrent_read_unit_test
	ToolRegistryConcurrentReadTest.cpp
	ToolRegistryConcurrentReadTest
)

dasall_add_tools_behavior_test(
	dasall_plugin_extension_bridge_unit_test
	PluginExtensionBridgeTest.cpp
	PluginExtensionBridgeTest
)

dasall_add_tools_behavior_test(
	dasall_plugin_extension_bridge_concurrency_unit_test
	PluginExtensionBridgeConcurrencyTest.cpp
	PluginExtensionBridgeConcurrencyTest
)

dasall_add_tools_behavior_test(
	dasall_tool_validator_unit_test
	ToolValidatorTest.cpp
	ToolValidatorTest
)

dasall_add_tools_behavior_test(
	dasall_tool_validator_defaulting_unit_test
	ToolValidatorDefaultingTest.cpp
	ToolValidatorDefaultingTest
)

dasall_add_tools_behavior_test(
	dasall_tool_validator_dry_run_unit_test
	ToolValidatorDryRunTest.cpp
	ToolValidatorDryRunTest
)

dasall_add_tools_behavior_test(
	dasall_tool_config_adapter_unit_test
	ToolConfigAdapterTest.cpp
	ToolConfigAdapterTest
)

dasall_add_tools_behavior_test(
	dasall_tool_config_adapter_hot_update_unit_test
	ToolConfigAdapterHotUpdateTest.cpp
	ToolConfigAdapterHotUpdateTest
)

dasall_add_tools_behavior_test(
	dasall_tool_policy_gate_unit_test
	ToolPolicyGateTest.cpp
	ToolPolicyGateTest
)

dasall_add_tools_behavior_test(
	dasall_tool_policy_profile_diff_unit_test
	ToolPolicyProfileDiffTest.cpp
	ToolPolicyProfileDiffTest
)

dasall_add_tools_behavior_test(
	dasall_tool_route_selector_unit_test
	ToolRouteSelectorTest.cpp
	ToolRouteSelectorTest
)

dasall_add_tools_behavior_test(
	dasall_tool_route_fallback_unit_test
	ToolRouteFallbackTest.cpp
	ToolRouteFallbackTest
)
