set(DASALL_ACCESS_UNIT_TEST_EXECUTABLE_TARGETS)

macro(dasall_add_access_unit_test target_name test_name source_file)
  add_executable(${target_name}
    ${source_file}
  )

  target_link_libraries(${target_name}
    PRIVATE
      dasall_test_support
      dasall_access
  )

  dasall_apply_common_options(${target_name})

  add_test(NAME ${test_name} COMMAND ${target_name})
  set_tests_properties(${test_name} PROPERTIES LABELS "unit;access")

  list(APPEND DASALL_ACCESS_UNIT_TEST_EXECUTABLE_TARGETS
    ${target_name}
  )
endmacro()

dasall_add_access_unit_test(
  dasall_access_interface_surface_unit_test
  AccessInterfaceSurfaceTest
  AccessInterfaceSurfaceTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_error_code_unit_test
  AccessErrorCodeTest
  AccessErrorCodeTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_error_mapping_unit_test
  AccessErrorMappingTest
  AccessErrorMappingTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_gateway_lifecycle_unit_test
  AccessGatewayLifecycleTest
  AccessGatewayLifecycleTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_supporting_types_unit_test
  AccessSupportingTypesTest
  AccessSupportingTypesTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_publish_envelope_unit_test
  PublishEnvelopeTypesTest
  PublishEnvelopeTypesTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_async_task_receipt_unit_test
  AsyncTaskReceiptTypesTest
  AsyncTaskReceiptTypesTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_admission_controller_surface_unit_test
  AdmissionControllerSurfaceTest
  AdmissionControllerSurfaceTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_runtime_bridge_surface_unit_test
  RuntimeBridgeSurfaceTest
  RuntimeBridgeSurfaceTest.cpp
)

dasall_add_access_unit_test(
  dasall_access_config_projection_unit_test
  AccessConfigProjectionTest
  AccessConfigProjectionTest.cpp
)

add_executable(dasall_access_daemon_protocol_adapter_local_trusted_unit_test
  DaemonProtocolAdapterLocalTrustedTest.cpp
)

target_link_libraries(dasall_access_daemon_protocol_adapter_local_trusted_unit_test
  PRIVATE
    dasall_test_support
    dasall_access
    dasall_platform
)

dasall_apply_common_options(dasall_access_daemon_protocol_adapter_local_trusted_unit_test)

add_test(NAME DaemonProtocolAdapterLocalTrustedTest
  COMMAND dasall_access_daemon_protocol_adapter_local_trusted_unit_test
)
set_tests_properties(DaemonProtocolAdapterLocalTrustedTest PROPERTIES LABELS "unit;access")

list(APPEND DASALL_ACCESS_UNIT_TEST_EXECUTABLE_TARGETS
  dasall_access_daemon_protocol_adapter_local_trusted_unit_test
)

add_executable(dasall_access_cli_ipc_client_unit_test
  CliIpcClientTest.cpp
  ../../../apps/cli/src/CliIpcClient.cpp
)

target_include_directories(dasall_access_cli_ipc_client_unit_test
  PRIVATE
    ../../../apps/cli/src
)

target_link_libraries(dasall_access_cli_ipc_client_unit_test
  PRIVATE
    dasall_test_support
    dasall_platform
)

dasall_apply_common_options(dasall_access_cli_ipc_client_unit_test)

add_test(NAME CliIpcClientTest COMMAND dasall_access_cli_ipc_client_unit_test)
set_tests_properties(CliIpcClientTest PROPERTIES LABELS "unit;access")

list(APPEND DASALL_ACCESS_UNIT_TEST_EXECUTABLE_TARGETS
  dasall_access_cli_ipc_client_unit_test
)

if(NOT DASALL_ACCESS_UNIT_TEST_EXECUTABLE_TARGETS)
  message(FATAL_ERROR "access unit registration requires at least one test target")
endif()

set(DASALL_ACCESS_UNIT_TEST_EXECUTABLE_TARGETS
  ${DASALL_ACCESS_UNIT_TEST_EXECUTABLE_TARGETS}
  PARENT_SCOPE
)
