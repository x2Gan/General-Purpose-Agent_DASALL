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
			${CMAKE_SOURCE_DIR}/infra/include
			${CMAKE_SOURCE_DIR}/profiles/include
			${CMAKE_SOURCE_DIR}/services/include
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

dasall_add_tools_behavior_test(
	dasall_mcp_adapter_unit_test
	MCPAdapterTest.cpp
	MCPAdapterTest
)

dasall_add_tools_behavior_test(
	dasall_mcp_adapter_transport_switch_unit_test
	MCPAdapterTransportSwitchTest.cpp
	MCPAdapterTransportSwitchTest
)

dasall_add_tools_behavior_test(
	dasall_mcp_lane_invoke_unit_test
	MCPLaneInvokeTest.cpp
	MCPLaneInvokeTest
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

dasall_add_tools_behavior_test(
	dasall_capability_cache_unit_test
	CapabilityCacheTest.cpp
	CapabilityCacheTest
)

dasall_add_tools_behavior_test(
	dasall_stdio_mcp_server_launcher_unit_test
	StdioMCPServerLauncherTest.cpp
	StdioMCPServerLauncherTest
)

dasall_add_tools_behavior_test(
	dasall_capability_discovery_unit_test
	CapabilityDiscoveryTest.cpp
	CapabilityDiscoveryTest
)

dasall_add_tools_behavior_test(
	dasall_tool_manager_skeleton_unit_test
	ToolManagerSkeletonTest.cpp
	ToolManagerSkeletonTest
)

dasall_add_tools_behavior_test(
	dasall_tool_manager_batch_invoke_unit_test
	ToolManagerBatchInvokeTest.cpp
	ToolManagerBatchInvokeTest
)

dasall_add_tools_behavior_test(
	dasall_tool_manager_pipeline_unit_test
	ToolManagerPipelineTest.cpp
	ToolManagerPipelineTest
)

dasall_add_tools_behavior_test(
	dasall_tool_manager_failure_path_unit_test
	ToolManagerFailurePathTest.cpp
	ToolManagerFailurePathTest
)

dasall_add_tools_behavior_test(
	dasall_tool_service_bridge_unit_test
	ToolServiceBridgeTest.cpp
	ToolServiceBridgeTest
)

dasall_add_tools_behavior_test(
	dasall_builtin_executor_lane_unit_test
	BuiltinExecutorLaneTest.cpp
	BuiltinExecutorLaneTest
)

dasall_add_tools_behavior_test(
	dasall_builtin_executor_lane_timeout_unit_test
	BuiltinExecutorLaneTimeoutTest.cpp
	BuiltinExecutorLaneTimeoutTest
)

dasall_add_tools_behavior_test(
	dasall_tool_audit_bridge_unit_test
	ToolAuditBridgeTest.cpp
	ToolAuditBridgeTest
)

dasall_add_tools_behavior_test(
	dasall_tool_metrics_bridge_unit_test
	ToolMetricsBridgeTest.cpp
	ToolMetricsBridgeTest
)

dasall_add_tools_behavior_test(
	dasall_tool_trace_bridge_unit_test
	ToolTraceBridgeTest.cpp
	ToolTraceBridgeTest
)

dasall_add_tools_behavior_test(
	dasall_tool_health_probe_unit_test
	ToolHealthProbeTest.cpp
	ToolHealthProbeTest
)

dasall_add_tools_behavior_test(
	dasall_result_projector_unit_test
	ResultProjectorTest.cpp
	ResultProjectorTest
)

dasall_add_tools_behavior_test(
	dasall_result_projector_truncation_unit_test
	ResultProjectorTruncationTest.cpp
	ResultProjectorTruncationTest
)

dasall_add_tools_behavior_test(
	dasall_result_projector_confidence_unit_test
	ResultProjectorConfidenceTest.cpp
	ResultProjectorConfidenceTest
)

dasall_add_tools_behavior_test(
	dasall_workflow_engine_unit_test
	WorkflowEngineTest.cpp
	WorkflowEngineTest
)

dasall_add_tools_behavior_test(
	dasall_compensation_ledger_unit_test
	CompensationLedgerTest.cpp
	CompensationLedgerTest
)

dasall_add_tools_behavior_test(
	dasall_skill_registry_unit_test
	SkillRegistryTest.cpp
	SkillRegistryTest
)

dasall_add_tools_behavior_test(
	dasall_skill_registry_priority_unit_test
	SkillRegistryPriorityTest.cpp
	SkillRegistryPriorityTest
)

dasall_add_tools_behavior_test(
	dasall_external_skill_importer_unit_test
	ExternalSkillImporterTest.cpp
	ExternalSkillImporterTest
)

dasall_add_tools_behavior_test(
	dasall_plugin_skill_bundle_importer_unit_test
	PluginSkillBundleImporterTest.cpp
	PluginSkillBundleImporterTest
)

dasall_add_tools_behavior_test(
	dasall_skill_runtime_instantiate_unit_test
	SkillRuntimeInstantiateTest.cpp
	SkillRuntimeInstantiateTest
)

dasall_add_tools_behavior_test(
	dasall_workflow_cyclic_rejection_unit_test
	WorkflowCyclicRejectionTest.cpp
	WorkflowCyclicRejectionTest
)

dasall_add_tools_behavior_test(
	dasall_tool_validator_boundary_unit_test
	ToolValidatorBoundaryTest.cpp
	ToolValidatorBoundaryTest
)

dasall_add_tools_behavior_test(
	dasall_capability_cache_concurrency_unit_test
	CapabilityCacheConcurrencyTest.cpp
	CapabilityCacheConcurrencyTest
)
